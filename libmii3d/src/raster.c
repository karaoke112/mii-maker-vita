#include "raster.h"

#include <math.h>

void m3_clear(m3_target *t, uint32_t argb)
{
    const uint8_t r = (uint8_t)(argb >> 16), g = (uint8_t)(argb >> 8), b = (uint8_t)argb, a = (uint8_t)(argb >> 24);
    const int n = t->width * t->height;
    for (int i = 0; i < n; i++) {
        uint8_t *p = t->rgba + 4 * i;
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    }
    if (t->depth)
        for (int i = 0; i < n; i++)
            t->depth[i] = 1.0f;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* sRGB-ish gamma 2.2, as the Switch Mii shader uses, via lookup tables */
#define GAMMA_STEPS 1024
static float g_to_linear[256];
static uint8_t g_to_srgb[GAMMA_STEPS + 1];
static int g_gamma_ready;

static void init_gamma(void)
{
    if (g_gamma_ready)
        return;
    for (int i = 0; i < 256; i++)
        g_to_linear[i] = powf(i / 255.0f, 2.2f);
    for (int i = 0; i <= GAMMA_STEPS; i++)
        g_to_srgb[i] = (uint8_t)(powf((float)i / GAMMA_STEPS, 1.0f / 2.2f) * 255.0f + 0.5f);
    g_gamma_ready = 1;
}

static inline float to_linear(float c) { return g_to_linear[(int)(clampf(c, 0.0f, 1.0f) * 255.0f + 0.5f)]; }
static inline uint8_t to_srgb8(float c) { return g_to_srgb[(int)(clampf(c, 0.0f, 1.0f) * GAMMA_STEPS + 0.5f)]; }

static inline void fetch(const mii3d_image_t *img, int x, int y, float out[4])
{
    const uint8_t *p = img->pixels + ((size_t)y * img->width + x) * img->channels;
    switch (img->channels) {
    case 1: out[0] = p[0] / 255.0f; out[1] = 0; out[2] = 0; out[3] = 1; break;
    case 2: out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f; out[2] = 0; out[3] = 1; break;
    default: out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[2] / 255.0f; out[3] = p[3] / 255.0f; break;
    }
}

/* Bilinear sample; u clamps or mirror-repeats, v clamps. */
static void sample(const mii3d_image_t *img, float u, float v, int mirror_u, float out[4])
{
    if (mirror_u) {
        float m = fmodf(fabsf(u), 2.0f);
        u = m > 1.0f ? 2.0f - m : m;
    }
    float fx = clampf(u, 0.0f, 1.0f) * img->width - 0.5f;
    float fy = clampf(v, 0.0f, 1.0f) * img->height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img->width - 1) x1 = img->width - 1;
    if (y1 > img->height - 1) y1 = img->height - 1;
    if (x0 > img->width - 1) x0 = img->width - 1;
    if (y0 > img->height - 1) y0 = img->height - 1;
    float a[4], b[4], c[4], d[4];
    fetch(img, x0, y0, a);
    fetch(img, x1, y0, b);
    fetch(img, x0, y1, c);
    fetch(img, x1, y1, d);
    for (int i = 0; i < 4; i++) {
        float top = a[i] + (b[i] - a[i]) * ax;
        float bot = c[i] + (d[i] - c[i]) * ax;
        out[i] = top + (bot - top) * ay;
    }
}

static void shade_fragment(const m3_material *m, float u, float v, float out[4])
{
    if (!m->tex || m->mode == M3_MOD_NONE) {
        memcpy(out, m->base, sizeof(float) * 4);
        return;
    }
    float t[4], c[4];
    sample(m->tex, u, v, m->mirror_u, t);
    switch (m->mode) {
    case M3_MOD_DIRECT:
        memcpy(c, t, sizeof(c));
        break;
    case M3_MOD_LAYERED_RGB:
        for (int i = 0; i < 3; i++)
            c[i] = t[0] * m->c[0][i] + t[1] * m->c[1][i] + t[2] * m->c[2][i];
        c[3] = t[3];
        break;
    case M3_MOD_ALPHA:
        c[0] = m->c[0][0]; c[1] = m->c[0][1]; c[2] = m->c[0][2]; c[3] = t[0];
        break;
    case M3_MOD_LUM_ALPHA:
        c[0] = t[0] * m->c[0][0]; c[1] = t[0] * m->c[0][1]; c[2] = t[0] * m->c[0][2]; c[3] = t[1];
        break;
    case M3_MOD_ALPHA_LUM:
        c[0] = t[1] * m->c[0][0]; c[1] = t[1] * m->c[0][1]; c[2] = t[1] * m->c[0][2]; c[3] = t[0];
        break;
    case M3_MOD_LUM:
    default:
        c[0] = t[0] * m->c[0][0]; c[1] = t[0] * m->c[0][1]; c[2] = t[0] * m->c[0][2]; c[3] = 1.0f;
        break;
    }
    /* texture over base color (base alpha 0 means no base) */
    if (m->base[3] > 0.0f) {
        for (int i = 0; i < 3; i++)
            out[i] = m->base[i] + (c[i] - m->base[i]) * c[3];
        out[3] = m->base[3] + (1.0f - m->base[3]) * c[3];
    } else {
        memcpy(out, c, sizeof(c));
    }
}

typedef struct sv { float x, y, z, iw, uw, vw, hl, add[3], rim[3]; } sv;

static inline float edge(const sv *a, const sv *b, float px, float py)
{
    return (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
}

static void draw_triangle(m3_target *t, const sv *a, const sv *b, const sv *c, const m3_material *m)
{
    float area = edge(a, b, c->x, c->y);
    if (fabsf(area) < 1e-8f)
        return;
    if (area < 0) { /* keep a consistent orientation; no culling */
        const sv *tmp = b; b = c; c = tmp;
        area = -area;
    }
    int minx = (int)floorf(fminf(a->x, fminf(b->x, c->x)));
    int maxx = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int miny = (int)floorf(fminf(a->y, fminf(b->y, c->y)));
    int maxy = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > t->width - 1) maxx = t->width - 1;
    if (maxy > t->height - 1) maxy = t->height - 1;
    const float inv = 1.0f / area;

    for (int y = miny; y <= maxy; y++) {
        const float py = y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            const float px = x + 0.5f;
            float w0 = edge(b, c, px, py), w1 = edge(c, a, px, py), w2 = edge(a, b, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            w0 *= inv; w1 *= inv; w2 *= inv;
            const size_t idx = (size_t)y * t->width + x;
            const float z = w0 * a->z + w1 * b->z + w2 * c->z;
            if (t->depth && m->depth_test && z - m->depth_bias > t->depth[idx])
                continue;
            const float iw = w0 * a->iw + w1 * b->iw + w2 * c->iw;
            const float pw = 1.0f / iw;
            const float u = (w0 * a->uw + w1 * b->uw + w2 * c->uw) * pw;
            const float v = (w0 * a->vw + w1 * b->vw + w2 * c->vw) * pw;
            float col[4];
            shade_fragment(m, u, v, col);
            if (m->faceline) {
                /* faceline: always opaque; feature texels mute spec/SSS */
                const float k = 1.0f - col[3] * m->sss_blend;
                const float hl = w0 * a->hl + w1 * b->hl + w2 * c->hl;
                for (int i = 0; i < 3; i++) {
                    const float add = w0 * a->add[i] + w1 * b->add[i] + w2 * c->add[i];
                    const float rim = w0 * a->rim[i] + w1 * b->rim[i] + w2 * c->rim[i];
                    col[i] = to_linear(col[i]) * hl + add * k + rim;
                }
                col[3] = 1.0f;
            } else if (m->lit) {
                if (col[3] <= 0.004f)
                    continue;
                const float hl = w0 * a->hl + w1 * b->hl + w2 * c->hl;
                for (int i = 0; i < 3; i++) {
                    const float add = w0 * a->add[i] + w1 * b->add[i] + w2 * c->add[i];
                    const float rim = w0 * a->rim[i] + w1 * b->rim[i] + w2 * c->rim[i];
                    col[i] = to_linear(col[i]) * hl + add + rim;
                }
            } else if (col[3] <= 0.004f) {
                continue;
            }
            uint8_t *p = t->rgba + 4 * idx;
            const float sa = col[3], da = p[3] / 255.0f;
            const float oa = sa + da * (1.0f - sa);
            for (int i = 0; i < 3; i++) {
                /* lit colors are linear; unlit (2D passes) stay sRGB */
                const float s = (m->lit || m->faceline) ? to_srgb8(col[i]) / 255.0f : clampf(col[i], 0.0f, 1.0f);
                const float d = p[i] / 255.0f;
                const float o = oa > 0 ? (s * sa + d * da * (1.0f - sa)) / oa : 0.0f;
                p[i] = (uint8_t)(clampf(o, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            p[3] = (uint8_t)(clampf(oa, 0.0f, 1.0f) * 255.0f + 0.5f);
            if (t->depth && m->depth_write && sa > 0.5f)
                t->depth[idx] = z;
        }
    }
}

void m3_draw(m3_target *t, const m3_vertex *verts, const uint16_t *indices, int index_count,
             const m3_material *mat)
{
    init_gamma();
    for (int i = 0; i + 2 < index_count; i += 3) {
        sv s[3];
        int ok = 1;
        for (int k = 0; k < 3; k++) {
            const m3_vertex *v = &verts[indices[i + k]];
            if (v->w <= 1e-4f) { /* behind the camera: no clipping needed for our framing */
                ok = 0;
                break;
            }
            const float iw = 1.0f / v->w;
            s[k].x = (v->x * iw * 0.5f + 0.5f) * t->width;
            s[k].y = (0.5f - v->y * iw * 0.5f) * t->height;
            s[k].z = v->z * iw;
            s[k].iw = iw;
            s[k].uw = v->u * iw;
            s[k].vw = v->v * iw;
            s[k].hl = v->hl;
            for (int c = 0; c < 3; c++) {
                s[k].add[c] = v->add[c];
                s[k].rim[c] = v->rim[c];
            }
        }
        if (ok)
            draw_triangle(t, &s[0], &s[1], &s[2], mat);
    }
}
