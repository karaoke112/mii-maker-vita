/* Builds and draws a Mii head from CharInfo + the face resource.
 *
 * Face-part layout (positions, scales, rotation offsets) and part
 * placement follow the vee/vfl project (MIT OR Apache-2.0), which
 * reimplements Nintendo's RFL/FFL behaviour; see THIRD_PARTY_NOTICES.md.
 */
#include "raster.h"

#include <mii/mii_editor.h>

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Switch Mii shading parameters per part (see raster.h for the formula). */
typedef struct m3_shading {
    float hl_factor, sss_blend;
    float sss[3];
    float spec[3];
    float spec_a, spec_b, shininess;
    float rim[3];
    float rim_power, rim_width;
} m3_shading;

#include "materials.inc"

/* ---- part cache --------------------------------------------------------- */

typedef struct tex_slot { int index; mii3d_image_t img; int ok; } tex_slot;
typedef struct mesh_slot { int index; mii3d_mesh_t mesh; int ok; } mesh_slot;

enum { SLOT_EYE, SLOT_EYEBROW, SLOT_MOUTH, SLOT_MUSTACHE, SLOT_MOLE, SLOT_GLASS, SLOT_NOSELINE,
       SLOT_WRINKLE, SLOT_MAKEUP, SLOT_BEARD, SLOT_TEX_COUNT };
enum { MS_FACELINE, MS_MASK, MS_HAIR, MS_FOREHEAD, MS_NOSE, MS_NOSELINE, MS_BEARD, MS_GLASS, MS_COUNT };

struct mii3d_renderer {
    const mii3d_res_t *res;
    tex_slot tex[SLOT_TEX_COUNT];
    mesh_slot mesh[MS_COUNT];
    /* scratch */
    mii3d_image_t face_tex, faceline_tex;
    float *depth;
    int depth_size;
    uint8_t *ss_color;
    int ss_size;
    /* the textures above are reused while the Mii is unchanged */
    mii_charinfo_t composed_for;
    int composed_valid, composed_has_faceline;
    m3_vertex *verts;
    int verts_cap;
};

static const mii3d_tex_kind_t k_slot_kind[SLOT_TEX_COUNT] = {
    MII3D_TEX_EYE, MII3D_TEX_EYEBROW, MII3D_TEX_MOUTH, MII3D_TEX_MUSTACHE, MII3D_TEX_MOLE,
    MII3D_TEX_GLASS, MII3D_TEX_NOSELINE, MII3D_TEX_FACELINE, MII3D_TEX_MAKEUP, MII3D_TEX_BEARD,
};
static const mii3d_shape_kind_t k_mesh_kind[MS_COUNT] = {
    MII3D_SHAPE_FACELINE, MII3D_SHAPE_MASK, MII3D_SHAPE_HAIR_NORMAL, MII3D_SHAPE_FOREHEAD_NORMAL,
    MII3D_SHAPE_NOSE, MII3D_SHAPE_NOSELINE, MII3D_SHAPE_BEARD, MII3D_SHAPE_GLASS,
};

static const mii3d_image_t *get_tex(mii3d_renderer_t *r, int slot, int index)
{
    tex_slot *s = &r->tex[slot];
    if (s->index != index) {
        mii3d_image_free(&s->img);
        s->index = index;
        s->ok = mii3d_res_texture(r->res, k_slot_kind[slot], index, &s->img) == MII3D_OK;
    }
    return s->ok ? &s->img : NULL;
}

static const mii3d_mesh_t *get_mesh(mii3d_renderer_t *r, int slot, int index)
{
    mesh_slot *s = &r->mesh[slot];
    if (s->index != index) {
        mii3d_mesh_free(&s->mesh);
        s->index = index;
        s->ok = mii3d_res_shape(r->res, k_mesh_kind[slot], index, &s->mesh) == MII3D_OK;
    }
    return s->ok ? &s->mesh : NULL;
}

mii3d_result_t mii3d_renderer_create(mii3d_renderer_t **out, const mii3d_res_t *res)
{
    if (!out || !res)
        return MII3D_ERR_ARGUMENT;
    mii3d_renderer_t *r = (mii3d_renderer_t *)calloc(1, sizeof(*r));
    if (!r)
        return MII3D_ERR_MEMORY;
    r->res = res;
    for (int i = 0; i < SLOT_TEX_COUNT; i++)
        r->tex[i].index = -1;
    for (int i = 0; i < MS_COUNT; i++)
        r->mesh[i].index = -1;
    *out = r;
    return MII3D_OK;
}

void mii3d_renderer_destroy(mii3d_renderer_t *r)
{
    if (!r)
        return;
    for (int i = 0; i < SLOT_TEX_COUNT; i++)
        mii3d_image_free(&r->tex[i].img);
    for (int i = 0; i < MS_COUNT; i++)
        mii3d_mesh_free(&r->mesh[i].mesh);
    mii3d_image_free(&r->face_tex);
    mii3d_image_free(&r->faceline_tex);
    free(r->depth);
    free(r->ss_color);
    free(r->verts);
    free(r);
}

void mii3d_view_init_default(mii3d_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->yaw_deg = 0.0f;
    view->face_texture_size = 256;
    view->supersample = 1;
}

static int ensure_image(mii3d_image_t *img, int w, int h)
{
    if (img->pixels && img->width == w && img->height == h)
        return 1;
    mii3d_image_free(img);
    img->pixels = (uint8_t *)malloc((size_t)w * h * 4);
    if (!img->pixels)
        return 0;
    img->width = (uint16_t)w;
    img->height = (uint16_t)h;
    img->channels = 4;
    return 1;
}

static m3_vertex *ensure_verts(mii3d_renderer_t *r, int n)
{
    if (n > r->verts_cap) {
        m3_vertex *v = (m3_vertex *)realloc(r->verts, sizeof(m3_vertex) * (size_t)n);
        if (!v)
            return NULL;
        r->verts = v;
        r->verts_cap = n;
    }
    return r->verts;
}

/* ---- colors ------------------------------------------------------------- */

static void common(float out[4], uint8_t c) { m3_rgb(out, mii_common_color_rgb(c), 1.0f); }
static void white(float out[4]) { out[0] = out[1] = out[2] = out[3] = 1.0f; }
static void black(float out[4]) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; }

/* ---- face texture: eyes, brows, mouth, mustache, mole -------------------- */

#define TEX_SCALE_X 0.88961464f
#define TEX_SCALE_Y 0.9276675f
#define RFL_Y_OFFSET 1.1600001f

static const uint8_t k_eye_rot_offset[80] = {
    3, 4, 4, 4, 3, 4, 4, 4, 3, 4, 4, 4, 4, 3, 3, 4, 4, 4, 3, 3, 4, 3, 4, 3, 3, 4, 3, 4, 4, 3,
    4, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 3, 4, 4, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};
static const uint8_t k_eyebrow_rot_offset[24] = {
    26, 26, 27, 25, 26, 25, 26, 25, 28, 25, 26, 24, 27, 27, 26, 26, 25, 25, 26, 26, 27, 26, 25, 27,
};

typedef enum origin { ORIGIN_CENTER, ORIGIN_LEFT, ORIGIN_RIGHT } origin;

typedef struct part { float x, y, w, h, angle; origin org; } part;

static float scale2dim(float s) { return 1.0f + 0.4f * s; }
static float rot2deg(int r) { return (360.0f / 32.0f) * (float)(r % 32); }

/* Draws one textured quad into a 2D target. Coordinates are in a res x res
 * space with y up, as RFL/FFL lay out the face. */
static void draw_part(m3_target *t, float res, const part *p, const m3_material *mat)
{
    float bx, s0, s1;
    switch (p->org) {
    case ORIGIN_CENTER: bx = -0.5f; s0 = 1.0f; s1 = 0.0f; break;
    case ORIGIN_RIGHT:  bx = -1.0f; s0 = 1.0f; s1 = 0.0f; break;
    case ORIGIN_LEFT:
    default:            bx = 0.0f;  s0 = 0.0f; s1 = 1.0f; break;
    }
    if (p->w <= 0 || p->h <= 0)
        return;
    /* local quad, texture row 0 at the top (+0.5) */
    const float lx[4] = { 1 + bx, 1 + bx, bx, bx }, ly[4] = { -0.5f, 0.5f, 0.5f, -0.5f };
    const float lu[4] = { s0, s0, s1, s1 }, lv[4] = { 1, 0, 0, 1 };
    const float a = -p->angle * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    const float sx = p->w * TEX_SCALE_X, sy = p->h * TEX_SCALE_Y;
    const float tx = p->x, ty = res - p->y;
    m3_vertex v[4];
    for (int i = 0; i < 4; i++) {
        const float x = lx[i] * sx, y = ly[i] * sy;
        const float wx = tx + x * ca - y * sa, wy = ty + x * sa + y * ca;
        v[i].x = wx / res * 2.0f - 1.0f;
        v[i].y = wy / res * 2.0f - 1.0f;
        v[i].z = 0.0f;
        v[i].w = 1.0f;
        v[i].u = lu[i];
        v[i].v = lv[i];
        v[i].hl = 1.0f;
        for (int c = 0; c < 3; c++)
            v[i].add[c] = v[i].rim[c] = 0.0f;
    }
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    m3_draw(t, v, idx, 6, mat);
}

static void compose_face(mii3d_renderer_t *r, const mii_charinfo_t *ci, m3_target *t)
{
    const float res = 256.0f, unit = res / 64.0f;
    m3_clear(t, 0x00000000u);
    m3_material m;
    part p;

    /* Eyes: r = outline (black), g = white, b = iris (eye color) */
    {
        const float scale = scale2dim(ci->eye_scale), sy = 0.12f * ci->eye_aspect + 0.64f;
        const float spacing = TEX_SCALE_X * ci->eye_x;
        const float y = 18.451525f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->eye_y;
        const float w = 342.0f / 64.0f * scale, h = 288.0f / 64.0f * scale * sy;
        const float ang = rot2deg(ci->eye_rotate + (32 - k_eye_rot_offset[ci->eye_type % 80]));
        const mii3d_image_t *tex = get_tex(r, SLOT_EYE, ci->eye_type);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_LAYERED_RGB;
            black(m.c[0]);
            white(m.c[1]);
            common(m.c[2], ci->eye_color);
            p = (part){ unit * (32.0f + spacing), y * unit, w * unit, h * unit, 360.0f - ang, ORIGIN_LEFT };
            draw_part(t, res, &p, &m);
            p = (part){ unit * (32.0f - spacing), y * unit, w * unit, h * unit, ang, ORIGIN_RIGHT };
            draw_part(t, res, &p, &m);
        }
    }
    /* Eyebrows: alpha texture in the eyebrow color */
    {
        const float scale = scale2dim(ci->eyebrow_scale), sy = 0.12f * ci->eyebrow_aspect + 0.64f;
        const float spacing = TEX_SCALE_X * ci->eyebrow_x;
        const float y = 16.549807f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->eyebrow_y;
        const float w = 324.0f / 64.0f * scale, h = 288.0f / 64.0f * scale * sy;
        const float ang = rot2deg(ci->eyebrow_rotate + k_eyebrow_rot_offset[ci->eyebrow_type % 24]);
        const mii3d_image_t *tex = get_tex(r, SLOT_EYEBROW, ci->eyebrow_type);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            common(m.c[0], ci->eyebrow_color);
            p = (part){ unit * (32.0f + spacing), y * unit, w * unit, h * unit, 360.0f - ang, ORIGIN_LEFT };
            draw_part(t, res, &p, &m);
            p = (part){ unit * (32.0f - spacing), y * unit, w * unit, h * unit, ang, ORIGIN_RIGHT };
            draw_part(t, res, &p, &m);
        }
    }
    /* Mouth: r = mouth color, g = upper lip, b = teeth (white) */
    {
        const float scale = scale2dim(ci->mouth_scale), sy = 0.12f * ci->mouth_aspect + 0.64f;
        const float y = 29.25885f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->mouth_y;
        const mii3d_image_t *tex = get_tex(r, SLOT_MOUTH, ci->mouth_type);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_LAYERED_RGB;
            common(m.c[0], ci->mouth_color);
            m3_rgb(m.c[1], mii_upper_lip_color_rgb(ci->mouth_color), 1.0f);
            white(m.c[2]);
            p = (part){ unit * 32.0f, y * unit, 396.0f / 64.0f * scale * unit, 288.0f / 64.0f * scale * sy * unit, 0, ORIGIN_CENTER };
            draw_part(t, res, &p, &m);
        }
    }
    /* Mustache (beard color), mirrored halves */
    if (ci->mustache_type != 0) {
        const float scale = scale2dim(ci->mustache_scale);
        const float y = 31.763554f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->mustache_y;
        const mii3d_image_t *tex = get_tex(r, SLOT_MUSTACHE, ci->mustache_type);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            common(m.c[0], ci->beard_color);
            p = (part){ unit * 32.0f, y * unit, 288.0f / 64.0f * scale * unit, 576.0f / 64.0f * scale * unit, 0, ORIGIN_LEFT };
            draw_part(t, res, &p, &m);
            p.org = ORIGIN_RIGHT;
            draw_part(t, res, &p, &m);
        }
    }
    /* Mole (black) */
    if (ci->mole_type != 0) {
        const float s = scale2dim(ci->mole_scale);
        const float x = 17.766165f + 2.0f * TEX_SCALE_X * ci->mole_x;
        const float y = 17.95986f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->mole_y;
        const mii3d_image_t *tex = get_tex(r, SLOT_MOLE, 1);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            black(m.c[0]);
            p = (part){ x * unit, y * unit, s * unit, s * unit, 0, ORIGIN_CENTER };
            draw_part(t, res, &p, &m);
        }
    }
}

/* ---- faceline texture: skin, wrinkles, makeup, texture beards ----------- */

static int compose_faceline(mii3d_renderer_t *r, const mii_charinfo_t *ci, m3_target *t)
{
    const int beard_tex = ci->beard_type >= 4 ? ci->beard_type - 3 : 0;
    if (ci->faceline_wrinkle == 0 && ci->faceline_make == 0 && beard_tex == 0)
        return 0;
    /* skin color everywhere, alpha = where layers were drawn: the Switch
     * shader uses that alpha to mute specular/SSS on those regions */
    m3_clear(t, mii_faceline_color_rgb(ci->faceline_color));
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
#define QV(X, Y, U, V) { .x = X, .y = Y, .z = 0, .w = 1, .u = U, .v = V, .hl = 1, .add = { 0, 0, 0 }, .rim = { 0, 0, 0 } }
    const m3_vertex quad[4] = { QV(-1, -1, 0, 1), QV(1, -1, 1, 1), QV(1, 1, 1, 0), QV(-1, 1, 0, 0) };
#undef QV
    m3_material m;
    if (ci->faceline_wrinkle != 0) {
        const mii3d_image_t *tex = get_tex(r, SLOT_WRINKLE, ci->faceline_wrinkle);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            m3_rgb(m.c[0], mii_faceline_color_rgb(ci->faceline_color), 1.0f);
            for (int i = 0; i < 3; i++)
                m.c[0][i] *= 0.55f; /* wrinkles: a darker shade of the skin */
            m3_draw(t, quad, idx, 6, &m);
        }
    }
    if (ci->faceline_make != 0) {
        const mii3d_image_t *tex = get_tex(r, SLOT_MAKEUP, ci->faceline_make);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_DIRECT;
            m3_draw(t, quad, idx, 6, &m);
        }
    }
    if (beard_tex) {
        const mii3d_image_t *tex = get_tex(r, SLOT_BEARD, beard_tex);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            common(m.c[0], ci->beard_color);
            m3_draw(t, quad, idx, 6, &m);
        }
    }
    return 1;
}

mii3d_result_t mii3d_render_face_texture(mii3d_renderer_t *r, const mii_charinfo_t *mii, uint8_t *rgba, int size)
{
    if (!r || !mii || !rgba || size <= 0)
        return MII3D_ERR_ARGUMENT;
    m3_target t = { size, size, rgba, NULL };
    compose_face(r, mii, &t);
    return MII3D_OK;
}

/* ---- 3D ----------------------------------------------------------------- */

typedef struct camera {
    float rot[9];       /* model rotation */
    float pivot[3];
    float eye_dist;
    float f, aspect, znear, zfar;
    float light[3], half[3];   /* view space */
} camera;

static void normalize3(float v[3])
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void setup_camera(camera *c, const mii3d_view_t *view, int w, int h)
{
    const float yaw = view->yaw_deg * (float)M_PI / 180.0f, pitch = view->pitch_deg * (float)M_PI / 180.0f;
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    /* R = Rx(pitch) * Ry(yaw) */
    const float r[9] = { cy, 0, sy, sp * sy, cp, -sp * cy, -cp * sy, sp, cp * cy };
    memcpy(c->rot, r, sizeof(r));
    c->pivot[0] = 0.0f; c->pivot[1] = 33.0f; c->pivot[2] = 0.0f;
    c->eye_dist = 330.0f;
    c->f = 1.0f / tanf(8.4f * (float)M_PI / 180.0f);
    c->aspect = (float)w / (float)h;
    c->znear = 100.0f;
    c->zfar = 600.0f;
    /* nn::mii's icon light direction (view space) */
    c->light[0] = -0.1227878f; c->light[1] = 0.70710677f; c->light[2] = 0.6963642f;
    normalize3(c->light);
    c->half[0] = c->light[0]; c->half[1] = c->light[1]; c->half[2] = c->light[2] + 1.0f;
    normalize3(c->half);
}

typedef struct placement { float scale[3]; float translate[3]; } placement;

static void transform_mesh(const camera *c, const mii3d_mesh_t *mesh, const placement *pl, const m3_shading *sh,
                           m3_vertex *out)
{
    for (int i = 0; i < mesh->vertex_count; i++) {
        float p[3], n[3], q[3], m[3];
        for (int k = 0; k < 3; k++) {
            p[k] = mesh->positions[3 * i + k] * pl->scale[k] + pl->translate[k] - c->pivot[k];
            n[k] = mesh->normals[3 * i + k] * (pl->scale[k] < 0 ? -1.0f : 1.0f);
        }
        for (int k = 0; k < 3; k++) {
            q[k] = c->rot[3 * k] * p[0] + c->rot[3 * k + 1] * p[1] + c->rot[3 * k + 2] * p[2];
            m[k] = c->rot[3 * k] * n[0] + c->rot[3 * k + 1] * n[1] + c->rot[3 * k + 2] * n[2];
        }
        if (m[0] == 0.0f && m[1] == 0.0f && m[2] == 0.0f)
            m[2] = 1.0f; /* the glasses quad has no normals */
        normalize3(m);
        /* camera on +z looking at the pivot */
        const float vz = q[2] - c->eye_dist;
        const float w = -vz;
        out[i].x = q[0] * c->f / c->aspect;
        out[i].y = q[1] * c->f;
        out[i].w = w;
        out[i].z = c->zfar * (w - c->znear) / (c->zfar - c->znear);
        out[i].u = mesh->uvs ? mesh->uvs[2 * i] : 0.0f;
        out[i].v = mesh->uvs ? mesh->uvs[2 * i + 1] : 0.0f;
        if (!sh) {
            out[i].hl = 1.0f;
            for (int k = 0; k < 3; k++)
                out[i].add[k] = out[i].rim[k] = 0.0f;
            continue;
        }
        float ndl = m[0] * c->light[0] + m[1] * c->light[1] + m[2] * c->light[2];
        float ndh = m[0] * c->half[0] + m[1] * c->half[1] + m[2] * c->half[2];
        if (ndl < 0) ndl = 0;
        if (ndh < 0) ndh = 0;
        const float hl = ndl * sh->hl_factor + (1.0f - sh->hl_factor);
        float factor = sh->spec_a;
        if (mesh->params) /* hair: blend between the two specular strengths */
            factor += (sh->spec_b - sh->spec_a) * (mesh->params[i] / 255.0f);
        const float spec = powf(ndh, sh->shininess) * factor;
        const float nz = m[2] < 0 ? 0 : (m[2] > 1 ? 1 : m[2]);
        const float rim_base = sh->rim_width * (1.0f - nz);
        const float rim = rim_base > 0 ? powf(rim_base, sh->rim_power) : 0.0f;
        out[i].hl = hl;
        for (int k = 0; k < 3; k++) {
            out[i].add[k] = spec * sh->spec[k] + sh->sss[k] * (1.0f - hl);
            out[i].rim[k] = sh->rim[k] * rim;
        }
    }
}

static void draw_mesh(mii3d_renderer_t *r, m3_target *t, const camera *c, const mii3d_mesh_t *mesh,
                      const placement *pl, const m3_material *mat, const m3_shading *sh)
{
    m3_vertex *v = ensure_verts(r, mesh->vertex_count);
    if (!v)
        return;
    transform_mesh(c, mesh, pl, (mat->lit || mat->faceline) ? sh : NULL, v);
    m3_draw(t, v, mesh->indices, mesh->index_count, mat);
}

static void placement_identity(placement *p)
{
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;
    p->translate[0] = p->translate[1] = p->translate[2] = 0.0f;
}

static mii3d_result_t render_head_1x(mii3d_renderer_t *r, const mii_charinfo_t *ci, const mii3d_view_t *view,
                                     uint8_t *rgba, int width, int height);

/* Box-filters an (w*ss) x (h*ss) image down to w x h. Color is weighted by
 * alpha so transparent background does not darken the edges. */
static void downsample(const uint8_t *src, uint8_t *dst, int w, int h, int ss)
{
    const int sw = w * ss, n = ss * ss;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned acc[3] = { 0, 0, 0 }, alpha = 0;
            for (int j = 0; j < ss; j++) {
                const uint8_t *p = src + ((size_t)(y * ss + j) * sw + (size_t)x * ss) * 4;
                for (int i = 0; i < ss; i++, p += 4) {
                    acc[0] += p[0] * p[3];
                    acc[1] += p[1] * p[3];
                    acc[2] += p[2] * p[3];
                    alpha += p[3];
                }
            }
            uint8_t *o = dst + ((size_t)y * w + x) * 4;
            for (int c = 0; c < 3; c++)
                o[c] = alpha ? (uint8_t)(acc[c] / alpha) : 0;
            o[3] = (uint8_t)(alpha / (unsigned)n);
        }
    }
}

mii3d_result_t mii3d_render_head(mii3d_renderer_t *r, const mii_charinfo_t *ci, const mii3d_view_t *view,
                                 uint8_t *rgba, int width, int height)
{
    mii3d_view_t def;
    if (!r || !rgba || width <= 0 || height <= 0)
        return MII3D_ERR_ARGUMENT;
    if (!view) {
        mii3d_view_init_default(&def);
        view = &def;
    }
    int ss = view->supersample < 1 ? 1 : (view->supersample > 3 ? 3 : view->supersample);
    if (ss == 1)
        return render_head_1x(r, ci, view, rgba, width, height);
    const int sw = width * ss, sh = height * ss;
    if (r->ss_size < sw * sh) {
        uint8_t *c = (uint8_t *)realloc(r->ss_color, (size_t)sw * sh * 4);
        if (!c)
            return MII3D_ERR_MEMORY;
        r->ss_color = c;
        r->ss_size = sw * sh;
    }
    mii3d_result_t res = render_head_1x(r, ci, view, r->ss_color, sw, sh);
    if (res == MII3D_OK)
        downsample(r->ss_color, rgba, width, height, ss);
    return res;
}

static mii3d_result_t render_head_1x(mii3d_renderer_t *r, const mii_charinfo_t *ci, const mii3d_view_t *view,
                                     uint8_t *rgba, int width, int height)
{
    mii3d_view_t def;
    if (!r || !ci || !rgba || width <= 0 || height <= 0)
        return MII3D_ERR_ARGUMENT;
    if (mii_verify(ci, 0) != MII_FIELD_COUNT && mii_verify(ci, 0) != MII_FIELD_COUNT + 1)
        return MII3D_ERR_ARGUMENT;
    if (!view) {
        mii3d_view_init_default(&def);
        view = &def;
    }
    if (r->depth_size < width * height) {
        float *d = (float *)realloc(r->depth, sizeof(float) * (size_t)width * height);
        if (!d)
            return MII3D_ERR_MEMORY;
        r->depth = d;
        r->depth_size = width * height;
    }
    const int fs = view->face_texture_size > 0 ? view->face_texture_size : 256;
    if (r->face_tex.width != fs)
        r->composed_valid = 0;
    if (!ensure_image(&r->face_tex, fs, fs) || !ensure_image(&r->faceline_tex, fs / 2, fs))
        return MII3D_ERR_MEMORY;

    if (!r->composed_valid || memcmp(&r->composed_for, ci, sizeof(*ci)) != 0 ||
        r->face_tex.width != fs) {
        m3_target face = { fs, fs, r->face_tex.pixels, NULL };
        compose_face(r, ci, &face);
        m3_target fl = { fs / 2, fs, r->faceline_tex.pixels, NULL };
        r->composed_has_faceline = compose_faceline(r, ci, &fl);
        r->composed_for = *ci;
        r->composed_valid = 1;
    }
    const int has_faceline_tex = r->composed_has_faceline;

    m3_target t = { width, height, rgba, r->depth };
    m3_clear(&t, view->background);

    camera cam;
    setup_camera(&cam, view, width, height);

    const mii3d_mesh_t *faceline = get_mesh(r, MS_FACELINE, ci->faceline_type);
    if (!faceline)
        return MII3D_ERR_RANGE;
    float skin[4];
    m3_rgb(skin, mii_faceline_color_rgb(ci->faceline_color), 1.0f);

    placement pl;
    m3_material m;

    /* faceline */
    placement_identity(&pl);
    const m3_shading *skin_mat = &k_mat_faceline[ci->faceline_color % 10];
    memset(&m, 0, sizeof(m));
    if (has_faceline_tex) {
        /* the texture carries the skin color; its alpha masks spec/SSS */
        m.mode = M3_MOD_DIRECT;
        m.tex = &r->faceline_tex;
        m.faceline = 1;
        m.sss_blend = skin_mat->sss_blend;
    } else {
        memcpy(m.base, skin, sizeof(skin));
        m.lit = 1;
    }
    m.depth_test = m.depth_write = 1;
    draw_mesh(r, &t, &cam, faceline, &pl, &m, skin_mat);

    /* forehead (skin under the hair) and hair, both follow the hair anchor */
    placement hair_pl;
    placement_identity(&hair_pl);
    memcpy(hair_pl.translate, faceline->hair_translate, sizeof(hair_pl.translate));
    if (ci->hair_flip)
        hair_pl.scale[0] = -1.0f;
    const mii3d_mesh_t *forehead = get_mesh(r, MS_FOREHEAD, ci->hair_type);
    if (forehead) {
        memset(&m, 0, sizeof(m));
        memcpy(m.base, skin, sizeof(skin));
        m.lit = 1;
        m.depth_test = m.depth_write = 1;
        draw_mesh(r, &t, &cam, forehead, &hair_pl, &m, skin_mat);
    }
    const mii3d_mesh_t *hair = get_mesh(r, MS_HAIR, ci->hair_type);
    if (hair) {
        memset(&m, 0, sizeof(m));
        common(m.base, ci->hair_color);
        m.lit = 1;
        m.depth_test = m.depth_write = 1;
        draw_mesh(r, &t, &cam, hair, &hair_pl, &m, &k_mat_hair[ci->hair_color % 100]);
    }

    /* beard shape (types 1-3) */
    if (ci->beard_type >= 1 && ci->beard_type <= 3) {
        const mii3d_mesh_t *beard = get_mesh(r, MS_BEARD, ci->beard_type);
        if (beard) {
            placement_identity(&pl);
            memcpy(pl.translate, faceline->beard_translate, sizeof(pl.translate));
            memset(&m, 0, sizeof(m));
            common(m.base, ci->beard_color);
            m.lit = 1;
            m.depth_test = m.depth_write = 1;
            draw_mesh(r, &t, &cam, beard, &pl, &m, &k_mat_beard[ci->beard_color % 100]);
        }
    }

    /* mask: the composed face texture */
    const mii3d_mesh_t *mask = get_mesh(r, MS_MASK, ci->faceline_type);
    if (mask) {
        placement_identity(&pl);
        memset(&m, 0, sizeof(m));
        m.tex = &r->face_tex;
        m.mode = M3_MOD_DIRECT;
        m.depth_test = 1;
        m.depth_bias = 0.002f;
        draw_mesh(r, &t, &cam, mask, &pl, &m, NULL); /* the mask material is unlit */
    }

    /* nose and nose line */
    placement nose_pl;
    {
        const float s = 0.175f * ci->nose_scale + 0.4f;
        nose_pl.scale[0] = nose_pl.scale[1] = nose_pl.scale[2] = s;
        nose_pl.translate[0] = faceline->nose_translate[0];
        nose_pl.translate[1] = faceline->nose_translate[1] + ((float)ci->nose_y - 8.0f) * -1.5f;
        nose_pl.translate[2] = faceline->nose_translate[2];
    }
    const mii3d_mesh_t *nose = get_mesh(r, MS_NOSE, ci->nose_type);
    if (nose) {
        memset(&m, 0, sizeof(m));
        memcpy(m.base, skin, sizeof(skin));
        m.lit = 1;
        m.depth_test = m.depth_write = 1;
        draw_mesh(r, &t, &cam, nose, &nose_pl, &m, &k_mat_nose[ci->faceline_color % 10]);
    }
    const mii3d_mesh_t *noseline = get_mesh(r, MS_NOSELINE, ci->nose_type);
    const mii3d_image_t *noseline_tex = get_tex(r, SLOT_NOSELINE, ci->nose_type);
    if (noseline && noseline_tex) {
        memset(&m, 0, sizeof(m));
        m.tex = noseline_tex;
        m.mode = M3_MOD_ALPHA;
        black(m.c[0]);
        m.depth_test = 1;
        m.depth_bias = 0.002f;
        draw_mesh(r, &t, &cam, noseline, &nose_pl, &m, NULL);
    }

    /* glasses: one lens texture, mirrored across the quad */
    if (ci->glass_type != 0) {
        const mii3d_mesh_t *glass = get_mesh(r, MS_GLASS, 0);
        /* Miitomo 2_3 has all 20 Switch styles; Wii U has the 9 Ver3 ones */
        const int glass_index = mii3d_res_texture_count(r->res, MII3D_TEX_GLASS) >= 20
            ? ci->glass_type : mii_glass_type_to_ver3(ci->glass_type);
        const mii3d_image_t *glass_tex = get_tex(r, SLOT_GLASS, glass_index);
        if (glass && glass_tex) {
            const float s = 0.15f * ci->glass_scale + 0.4f;
            pl.scale[0] = pl.scale[1] = pl.scale[2] = s;
            pl.translate[0] = faceline->nose_translate[0];
            pl.translate[1] = faceline->nose_translate[1] + ((float)ci->glass_y - 11.0f) * -1.5f + 5.0f;
            pl.translate[2] = faceline->nose_translate[2] + 2.0f;
            memset(&m, 0, sizeof(m));
            m.tex = glass_tex;
            /* r = coverage, g = shade: frames take the glass color,
             * sunglasses get dark frames and tinted, see-through lenses */
            m.mode = M3_MOD_ALPHA_LUM;
            common(m.c[0], ci->glass_color);
            m.mirror_u = 1;
            m.lit = 1;
            m.depth_test = 1;
            draw_mesh(r, &t, &cam, glass, &pl, &m, &k_mat_glass[0]);
        }
    }
    return MII3D_OK;
}
