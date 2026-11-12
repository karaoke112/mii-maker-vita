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
       SLOT_WRINKLE, SLOT_MAKEUP, SLOT_BEARD, SLOT_CAP, SLOT_TEX_COUNT };
enum { MS_FACELINE, MS_MASK, MS_HAIR, MS_FOREHEAD, MS_NOSE, MS_NOSELINE, MS_BEARD, MS_GLASS, MS_HAT, MS_COUNT };

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
    uint8_t composed_for[32]; /* face_key() of the composed Mii */
    int composed_valid, composed_has_faceline;
    m3_vertex *verts;
    int verts_cap;
    /* body: per gender, the mesh and its indices split into shirt / pants */
    const mii3d_res_t *body_res;
    mii3d_mesh_t body[2];
    uint16_t *body_idx[2];
    int body_shirt_count[2], body_pants_count[2];
    int body_loaded[2];
    /* mii3d_render_head_draws: 3D draws go here instead of the rasterizer */
    mii3d_draw_fn draw_fn;
    void *draw_user;
};

static const mii3d_tex_kind_t k_slot_kind[SLOT_TEX_COUNT] = {
    MII3D_TEX_EYE, MII3D_TEX_EYEBROW, MII3D_TEX_MOUTH, MII3D_TEX_MUSTACHE, MII3D_TEX_MOLE,
    MII3D_TEX_GLASS, MII3D_TEX_NOSELINE, MII3D_TEX_FACELINE, MII3D_TEX_MAKEUP, MII3D_TEX_BEARD,
    MII3D_TEX_CAP,
};
static const mii3d_shape_kind_t k_mesh_kind[MS_COUNT] = {
    MII3D_SHAPE_FACELINE, MII3D_SHAPE_MASK, MII3D_SHAPE_HAIR_NORMAL, MII3D_SHAPE_FOREHEAD_NORMAL,
    MII3D_SHAPE_NOSE, MII3D_SHAPE_NOSELINE, MII3D_SHAPE_BEARD, MII3D_SHAPE_GLASS,
    MII3D_SHAPE_HAT_NORMAL,
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

static void body_free(mii3d_renderer_t *r)
{
    for (int g = 0; g < 2; g++) {
        mii3d_mesh_free(&r->body[g]);
        free(r->body_idx[g]);
        r->body_idx[g] = NULL;
        r->body_loaded[g] = 0;
    }
}

void mii3d_renderer_set_body(mii3d_renderer_t *r, const mii3d_res_t *body)
{
    if (!r)
        return;
    body_free(r);
    r->body_res = body;
}

/* The body meshes are laid out for a texture atlas whose bottom band
 * (v > ~0.66 to 0.72) holds the legs and feet: those triangles, and the
 * soles, are the pants. */
static const mii3d_mesh_t *get_body(mii3d_renderer_t *r, int gender)
{
    gender = gender ? 1 : 0;
    if (!r->body_res)
        return NULL;
    if (!r->body_loaded[gender]) {
        mii3d_mesh_t *m = &r->body[gender];
        if (mii3d_res_body(r->body_res, gender, m) != MII3D_OK || !m->uvs)
            return NULL;
        uint16_t *idx = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)(m->index_count ? m->index_count : 1));
        if (!idx) {
            mii3d_mesh_free(m);
            return NULL;
        }
        int shirt = 0, pants = m->index_count;
        /* the male shirt is tucked in (pants up to the waist); the female
         * top is a dress that covers the top of the legs */
        const float waist = gender ? 0.718f : 0.66f;
        for (int i = 0; i < m->index_count; i += 3) {
            const float v = (m->uvs[2 * m->indices[i] + 1] + m->uvs[2 * m->indices[i + 1] + 1] +
                             m->uvs[2 * m->indices[i + 2] + 1]) / 3.0f;
            const float *p = m->positions;
            const int foot = p[3 * m->indices[i] + 1] < 7.0f && p[3 * m->indices[i + 1] + 1] < 7.0f &&
                             p[3 * m->indices[i + 2] + 1] < 7.0f; /* shoes and soles */
            uint16_t *dst = (v > waist || foot) ? idx + (pants -= 3) : idx + (shirt += 3) - 3;
            dst[0] = m->indices[i]; dst[1] = m->indices[i + 1]; dst[2] = m->indices[i + 2];
        }
        r->body_idx[gender] = idx;
        r->body_shirt_count[gender] = shirt;
        r->body_pants_count[gender] = m->index_count - shirt;
        r->body_loaded[gender] = 1;
    }
    return &r->body[gender];
}

void mii3d_renderer_destroy(mii3d_renderer_t *r)
{
    if (!r)
        return;
    body_free(r);
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
    view->zoom = 1.0f;
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
static void submit(mii3d_renderer_t *r, m3_target *t, const m3_vertex *v, int vertex_count, const uint16_t *idx,
                   int index_count, const m3_material *m);

static void draw_part(mii3d_renderer_t *r, m3_target *t, float res, const part *p, const m3_material *mat)
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
    submit(r, t, v, 4, idx, 6, mat);
}

enum { FACE_EYES = 1, FACE_BROWS = 2, FACE_MOUTH = 4, FACE_MUSTACHE = 8, FACE_MOLE = 16, FACE_ALL = 31 };

static void compose_face(mii3d_renderer_t *r, const mii_charinfo_t *ci, m3_target *t, unsigned parts)
{
    const float res = 256.0f, unit = res / 64.0f;
    if (t->rgba)
        m3_clear(t, 0x00000000u);
    m3_material m;
    part p;

    /* Eyes: r = outline (black), g = white, b = iris (eye color) */
    if (parts & FACE_EYES) {
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
            /* r: the eyeshadow band (only eye type 20 has one), which the
             * Switch 2 draws light blue; the lines and pupil are r = g = b = 0 */
            m3_rgb(m.c[0], 0x00FFFFu, 1.0f);
            white(m.c[1]);
            common(m.c[2], ci->eye_color);
            p = (part){ unit * (32.0f + spacing), y * unit, w * unit, h * unit, 360.0f - ang, ORIGIN_LEFT };
            draw_part(r, t, res, &p, &m);
            p = (part){ unit * (32.0f - spacing), y * unit, w * unit, h * unit, ang, ORIGIN_RIGHT };
            draw_part(r, t, res, &p, &m);
        }
    }
    /* Eyebrows: alpha texture in the eyebrow color */
    if (parts & FACE_BROWS) {
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
            draw_part(r, t, res, &p, &m);
            p = (part){ unit * (32.0f - spacing), y * unit, w * unit, h * unit, ang, ORIGIN_RIGHT };
            draw_part(r, t, res, &p, &m);
        }
    }
    /* Mouth: r = mouth color, g = upper lip, b = teeth (white) */
    if (parts & FACE_MOUTH) {
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
            draw_part(r, t, res, &p, &m);
        }
    }
    /* Mustache (beard color), mirrored halves */
    if ((parts & FACE_MUSTACHE) && ci->mustache_type != 0) {
        const float scale = scale2dim(ci->mustache_scale);
        const float y = 31.763554f + RFL_Y_OFFSET * TEX_SCALE_Y * ci->mustache_y;
        const mii3d_image_t *tex = get_tex(r, SLOT_MUSTACHE, ci->mustache_type);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            common(m.c[0], ci->beard_color);
            p = (part){ unit * 32.0f, y * unit, 288.0f / 64.0f * scale * unit, 576.0f / 64.0f * scale * unit, 0, ORIGIN_LEFT };
            draw_part(r, t, res, &p, &m);
            p.org = ORIGIN_RIGHT;
            draw_part(r, t, res, &p, &m);
        }
    }
    /* Mole (black) */
    if ((parts & FACE_MOLE) && ci->mole_type != 0) {
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
            draw_part(r, t, res, &p, &m);
        }
    }
}

/* The fields the face and faceline textures depend on: Miis that differ
 * only in hair, nose, glasses, face shape or body reuse the textures
 * (this is what makes a grid of hair or nose icons cheap). */
static void face_key(const mii_charinfo_t *ci, uint8_t out[32])
{
    const uint8_t k[] = {
        ci->eye_type, ci->eye_color, ci->eye_scale, ci->eye_aspect, ci->eye_rotate, ci->eye_x, ci->eye_y,
        ci->eyebrow_type, ci->eyebrow_color, ci->eyebrow_scale, ci->eyebrow_aspect, ci->eyebrow_rotate,
        ci->eyebrow_x, ci->eyebrow_y,
        ci->mouth_type, ci->mouth_color, ci->mouth_scale, ci->mouth_aspect, ci->mouth_y,
        ci->mustache_type, ci->mustache_scale, ci->mustache_y, ci->beard_type, ci->beard_color,
        ci->mole_type, ci->mole_scale, ci->mole_x, ci->mole_y,
        ci->faceline_color, ci->faceline_wrinkle, ci->faceline_make, ci->gender,
    };
    memcpy(out, k, 32);
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
            m.mix_rgb = 1;
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
            m.mix_rgb = 1;
            m3_draw(t, quad, idx, 6, &m);
        }
    }
    if (beard_tex) {
        const mii3d_image_t *tex = get_tex(r, SLOT_BEARD, beard_tex);
        if (tex) {
            memset(&m, 0, sizeof(m));
            m.tex = tex;
            m.mode = M3_MOD_ALPHA;
            m.mix_rgb = 1;
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
    compose_face(r, mii, &t, FACE_ALL);
    return MII3D_OK;
}

/* ---- 3D ----------------------------------------------------------------- */

typedef struct camera {
    float rot[9];       /* model rotation */
    float pivot[3];
    /* where the current model sits: the head on the body's neck, or the
     * body scaled for height and build */
    float mscale[3], moffset[3];
    int front; /* within 60 degrees of facing the camera */
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
    for (int k = 0; k < 3; k++) {
        c->mscale[k] = 1.0f;
        c->moffset[k] = 0.0f;
    }
    c->front = cosf(yaw) * cosf(pitch) >= 0.5f;
    c->pivot[0] = 0.0f; c->pivot[1] = 33.0f + view->center_y; c->pivot[2] = 0.0f;
    c->eye_dist = 330.0f;
    c->f = (view->zoom > 0 ? view->zoom : 1.0f) / tanf(8.4f * (float)M_PI / 180.0f);
    c->aspect = (float)w / (float)h;
    c->znear = 100.0f;
    c->zfar = 600.0f;
    if (view->full_body) {
        /* the same lens from further back: room for the tallest Mii */
        c->pivot[1] = 92.0f + view->center_y;
        c->eye_dist = 720.0f;
        c->znear = 450.0f;
        c->zfar = 1000.0f;
    }
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
            p[k] = (mesh->positions[3 * i + k] * pl->scale[k] + pl->translate[k]) * c->mscale[k] + c->moffset[k] -
                   c->pivot[k];
            n[k] = mesh->normals[3 * i + k] * (pl->scale[k] < 0 ? -1.0f : 1.0f) / c->mscale[k];
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

/* Back faces are skipped for the closed shapes when the Mii faces the
 * camera (within 60 degrees): from there they are hidden anyway, and icons
 * draw about half the triangles. Seen from further round, the open backs
 * of the face and hair shapes show their inside, so both sides are drawn. */
static int cull_for(const mii3d_renderer_t *r, const camera *c, const mii3d_mesh_t *mesh, const placement *pl)
{
    if (!c->front)
        return 0;
    static const int cullable[] = { MS_FACELINE, MS_HAIR, MS_FOREHEAD, MS_NOSE, MS_BEARD };
    for (size_t k = 0; k < sizeof(cullable) / sizeof(cullable[0]); k++)
        if (mesh == &r->mesh[cullable[k]].mesh)
            return pl->scale[0] < 0 ? 1 : -1; /* mirrored hair flips the winding */
    return 0;
}

/* Rasterizes, or hands the draw to mii3d_render_head_draws' callback. */
static void submit(mii3d_renderer_t *r, m3_target *t, const m3_vertex *v, int vertex_count, const uint16_t *idx,
                   int index_count, const m3_material *m)
{
    if (t->rgba) {
        m3_draw(t, v, idx, index_count, m);
        return;
    }
    if (!r->draw_fn)
        return;
    mii3d_draw_t d;
    memset(&d, 0, sizeof(d));
    d.vertices = &v[0].x;
    d.vertex_count = vertex_count;
    d.indices = idx;
    d.index_count = index_count;
    d.texture = m->mode == M3_MOD_NONE ? NULL : m->tex;
    d.texture_dynamic = m->tex == &r->face_tex || m->tex == &r->faceline_tex;
    d.mirror_u = m->mirror_u;
    d.mode = (mii3d_modulate_t)m->mode;
    memcpy(d.base, m->base, sizeof(d.base));
    memcpy(d.c, m->c, sizeof(d.c));
    d.lit = m->lit;
    d.faceline = m->faceline;
    d.sss_blend = m->sss_blend;
    d.depth_test = m->depth_test;
    d.depth_write = m->depth_write;
    d.depth_bias = m->depth_bias;
    r->draw_fn(r->draw_user, &d);
}

static void draw_mesh(mii3d_renderer_t *r, m3_target *t, const camera *c, const mii3d_mesh_t *mesh,
                      const placement *pl, const m3_material *mat_in, const m3_shading *sh)
{
    m3_material mm = *mat_in;
    const m3_material *mat = &mm;
    mm.cull = cull_for(r, c, mesh, pl);
    m3_vertex *v = ensure_verts(r, mesh->vertex_count);
    if (!v)
        return;
    transform_mesh(c, mesh, pl, (mat->lit || mat->faceline) ? sh : NULL, v);
    submit(r, t, v, mesh->vertex_count, mesh->indices, mesh->index_count, mat);
}

static void placement_identity(placement *p)
{
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;
    p->translate[0] = p->translate[1] = p->translate[2] = 0.0f;
}

static mii3d_result_t render_head_1x(mii3d_renderer_t *r, const mii_charinfo_t *ci, const mii3d_view_t *view,
                                     uint8_t *rgba, int width, int height);

/* Body proportions from height and build (0-127), as in nn::mii's
 * GetBodyScale (see Arian Kordi's FFL-Testing, public domain). */
static void body_scale(const mii_charinfo_t *ci, float out[3])
{
    const float h = (float)ci->height, b = (float)ci->build;
    out[0] = b * (h * 0.003671875f + 0.4f) / 128.0f + h * 0.001796875f + 0.4f;
    out[1] = h * 0.006015625f + 0.5f;
    out[2] = out[0];
}

/* The neck in body space (FFLBodyRes head height x the body-to-head scale
 * used for these meshes, per FFL-Testing's body table). */
#define BODY_HEAD_Y (10.7766f * 8.715f)
/* nn::mii's normal ("own Mii") pants color */
#define PANTS_RGB 0x40464Eu

/* Draws the body and leaves the camera set to put the head on its neck. */
static void draw_body(mii3d_renderer_t *r, m3_target *t, camera *c, const mii_charinfo_t *ci)
{
    float scale[3];
    body_scale(ci, scale);
    const mii3d_mesh_t *body = get_body(r, ci->gender);
    if (body) {
        const int g = ci->gender ? 1 : 0;
        memcpy(c->mscale, scale, sizeof(scale));
        placement pl;
        placement_identity(&pl);
        m3_vertex *v = ensure_verts(r, body->vertex_count);
        if (v) {
            m3_material m;
            memset(&m, 0, sizeof(m));
            m3_rgb(m.base, mii_favorite_color_rgb(ci->favorite_color), 1.0f);
            m.lit = 1;
            m.depth_test = m.depth_write = 1;
            transform_mesh(c, body, &pl, &k_mat_body[ci->favorite_color % 12], v);
            submit(r, t, v, body->vertex_count, r->body_idx[g], r->body_shirt_count[g], &m);
            m3_rgb(m.base, PANTS_RGB, 1.0f);
            transform_mesh(c, body, &pl, &k_mat_pants[0], v);
            submit(r, t, v, body->vertex_count, r->body_idx[g] + r->body_shirt_count[g], r->body_pants_count[g], &m);
        }
    }
    for (int k = 0; k < 3; k++)
        c->mscale[k] = 1.0f;
    c->moffset[1] = BODY_HEAD_Y * scale[1];
}

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

mii3d_result_t mii3d_render_head_draws(mii3d_renderer_t *r, const mii_charinfo_t *ci, const mii3d_view_t *view,
                                       int width, int height, mii3d_draw_fn fn, void *user)
{
    mii3d_view_t def;
    if (!r || !ci || !fn || width <= 0 || height <= 0)
        return MII3D_ERR_ARGUMENT;
    if (!view) {
        mii3d_view_init_default(&def);
        view = &def;
    }
    r->draw_fn = fn;
    r->draw_user = user;
    mii3d_result_t res = render_head_1x(r, ci, view, NULL, width, height);
    r->draw_fn = NULL;
    r->draw_user = NULL;
    return res;
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
    if (!r || !ci || width <= 0 || height <= 0)
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

    uint8_t key[sizeof(r->composed_for)];
    face_key(ci, key);
    if (!r->composed_valid || memcmp(r->composed_for, key, sizeof(key)) != 0 || r->face_tex.width != fs) {
        m3_target face = { fs, fs, r->face_tex.pixels, NULL };
        compose_face(r, ci, &face, FACE_ALL);
        m3_target fl = { fs / 2, fs, r->faceline_tex.pixels, NULL };
        r->composed_has_faceline = compose_faceline(r, ci, &fl);
        memcpy(r->composed_for, key, sizeof(key));
        r->composed_valid = 1;
    }
    const int has_faceline_tex = r->composed_has_faceline;

    /* rgba NULL: mii3d_render_head_draws (no pixels, no depth buffer) */
    m3_target t = { width, height, rgba, rgba ? r->depth : NULL };
    if (rgba)
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

    if (view->full_body)
        draw_body(r, &t, &cam, ci);

    /* faceline */
    placement_identity(&pl);
    const m3_shading *skin_mat = &k_mat_faceline[ci->faceline_color % 10];
    memset(&m, 0, sizeof(m));
    if (has_faceline_tex) {
        /* the texture carries the skin color; its alpha masks spec/SSS */
        m.mode = M3_MOD_DIRECT;
        m.tex = &r->faceline_tex;
        m.mirror_u = 1; /* the texture is one half of the face; u runs 0..2 */
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
    /* hats: some hairstyles come with one (caps, beanies, headbands), in the
     * favorite color, shaded by the hat's texture */
    const mii3d_mesh_t *hat = get_mesh(r, MS_HAT, ci->hair_type);
    if (hat && hat->vertex_count > 0) {
        memset(&m, 0, sizeof(m));
        m3_rgb(m.base, mii_favorite_color_rgb(ci->favorite_color), 1.0f);
        const mii3d_image_t *cap = hat->uvs ? get_tex(r, SLOT_CAP, ci->hair_type) : NULL;
        if (cap) {
            m.tex = cap;
            m.mode = M3_MOD_LUM;
            memcpy(m.c[0], m.base, sizeof(m.c[0]));
        }
        m.lit = 1;
        m.depth_test = m.depth_write = 1;
        draw_mesh(r, &t, &cam, hat, &hair_pl, &m, &k_mat_hair[ci->hair_color % 100]);
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

/* ---- part icons --------------------------------------------------------- */

/* Copies the opaque bounding box of `src` (res x res) into a size x size
 * image, scaled to fit with a margin and centered. */
/* Cuts the drawn part out of `src` (res x res) into a size x size icon,
 * centered. extent: the frame's side in pixels at res 384 (0: fit the part). */
static void crop_to_icon(const uint8_t *src, int res, uint8_t *dst, int size, float extent)
{
    int x0 = res, y0 = res, x1 = -1, y1 = -1;
    for (int y = 0; y < res; y++)
        for (int x = 0; x < res; x++)
            if (src[4 * (y * res + x) + 3] > 8) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    memset(dst, 0, (size_t)size * size * 4);
    if (x1 < 0)
        return;
    const float bw = (float)(x1 - x0 + 1), bh = (float)(y1 - y0 + 1);
#ifdef MII3D_ICON_DEBUG
    fprintf(stderr, "icon bbox %.0f x %.0f of %d\n", bw, bh, res);
#endif
    /* A fixed frame (the part's largest type) keeps every type of a part at
     * the same scale, as on the Switch: small eyes look small. */
    float span = bw > bh ? bw : bh;
    if (extent > 0.0f && extent * res / 384.0f > span)
        span = extent * res / 384.0f;
    const float fit = size * 0.84f / span;
    const float ox = (size - bw * fit) / 2, oy = (size - bh * fit) / 2;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            /* box-filter the source footprint of this output pixel */
            const float sx0 = x0 + (x - ox) / fit, sy0 = y0 + (y - oy) / fit;
            const float step = 1.0f / fit;
            const int n = step > 1.0f ? (int)ceilf(step) : 1;
            float acc[3] = { 0, 0, 0 }, alpha = 0;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    const int sx = (int)(sx0 + (i + 0.5f) * step / n), sy = (int)(sy0 + (j + 0.5f) * step / n);
                    if (sx < x0 || sy < y0 || sx > x1 || sy > y1)
                        continue;
                    const uint8_t *p = src + 4 * (sy * res + sx);
                    acc[0] += p[0] * p[3]; acc[1] += p[1] * p[3]; acc[2] += p[2] * p[3];
                    alpha += p[3];
                }
            uint8_t *o = dst + 4 * (y * size + x);
            if (alpha > 0) {
                o[0] = (uint8_t)(acc[0] / alpha);
                o[1] = (uint8_t)(acc[1] / alpha);
                o[2] = (uint8_t)(acc[2] / alpha);
                o[3] = (uint8_t)(alpha / (float)(n * n));
            }
        }
    }
}

int mii3d_hair_has_hat(mii3d_renderer_t *r, int hair_type)
{
    if (!r)
        return 0;
    const mii3d_mesh_t *hat = get_mesh(r, MS_HAT, hair_type);
    return hat && hat->vertex_count > 0;
}

int mii3d_part_shows_color(mii3d_renderer_t *r, mii3d_icon_part_t part, int type)
{
    if (!r || part != MII3D_ICON_MOUTH)
        return 1;
    const mii3d_image_t *tex = get_tex(r, SLOT_MOUTH, type);
    if (!tex || tex->channels < 2)
        return 1;
    /* r = mouth color, g = upper lip: any visible pixel of either */
    const int ch = tex->channels;
    int count = 0;
    for (int i = 0; i < tex->width * tex->height; i++) {
        const uint8_t *p = tex->pixels + (size_t)i * ch;
        const int a = ch >= 4 ? p[3] : 255;
        if (a > 64 && (p[0] > 64 || p[1] > 64))
            count++;
    }
    return count > 8;
}

int mii3d_part_icon_res(int size)
{
    /* compose at 384 for icons up to ~170 px (about half the pixels of 512) */
    return size <= 170 ? 384 : 512;
}

/* Draws the part alone into `t` (res x res, cleared to transparent), or
 * hands its draws to the renderer's callback when t->rgba is NULL. */
static mii3d_result_t part_icon_compose(mii3d_renderer_t *r, const mii_charinfo_t *mii, mii3d_icon_part_t part,
                                        m3_target *t)
{
    /* the part's own type and color, default size and placement */
    mii_charinfo_t ci;
    mii_charinfo_init_default(&ci, mii->gender);
    ci.eye_type = mii->eye_type;          ci.eye_color = mii->eye_color;
    ci.eyebrow_type = mii->eyebrow_type;  ci.eyebrow_color = mii->eyebrow_color;
    ci.mouth_type = mii->mouth_type;      ci.mouth_color = mii->mouth_color;
    ci.mustache_type = mii->mustache_type; ci.beard_color = mii->beard_color;
    ci.mole_type = 1;                      ci.mole_scale = 8;

    switch (part) {
    case MII3D_ICON_EYES:     compose_face(r, &ci, t, FACE_EYES); break;
    case MII3D_ICON_EYEBROWS: compose_face(r, &ci, t, FACE_BROWS); break;
    case MII3D_ICON_EYE:      compose_face(r, &ci, t, FACE_EYES); break;
    case MII3D_ICON_EYEBROW:  compose_face(r, &ci, t, FACE_BROWS); break;
    case MII3D_ICON_MOUTH:    compose_face(r, &ci, t, FACE_MOUTH); break;
    case MII3D_ICON_MUSTACHE: compose_face(r, &ci, t, FACE_MUSTACHE); break;
    case MII3D_ICON_MOLE:     compose_face(r, &ci, t, FACE_MOLE); break;
    case MII3D_ICON_NOSE: {
        /* the nose's line drawing (the Switch shows these), in black */
        if (t->rgba)
            m3_clear(t, 0x00000000u);
        const mii3d_image_t *tex = get_tex(r, SLOT_NOSELINE, mii->nose_type);
        if (!tex)
            break;
        m3_material m;
        memset(&m, 0, sizeof(m));
        m.tex = tex;
        m.mode = M3_MOD_ALPHA;
        m.c[0][3] = 1.0f;
#define NV(X, Y, U, V) { .x = X, .y = Y, .z = 0, .w = 1, .u = U, .v = V, .hl = 1, .add = { 0, 0, 0 }, .rim = { 0, 0, 0 } }
        static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
        if (mii->nose_type == 11) {
            /* this texture keeps the nostrils far below the lines (on the
             * face the mesh brings them up): as the Switch 2 shows it, the
             * part below row 192 is drawn 64 rows (of 256) higher, the
             * nostrils between the splayed ends of the lines */
            const float cut = 192.0f / 256.0f, ycut = 1.0f - 2.0f * cut, up = 2.0f * 64.0f / 256.0f;
            const m3_vertex top[4] = { NV(-1, ycut, 0, cut), NV(1, ycut, 1, cut), NV(1, 1, 1, 0), NV(-1, 1, 0, 0) };
            const m3_vertex low[4] = { NV(-1, -1 + up, 0, 1), NV(1, -1 + up, 1, 1), NV(1, ycut + up, 1, cut),
                                       NV(-1, ycut + up, 0, cut) };
            submit(r, t, top, 4, idx, 6, &m);
            submit(r, t, low, 4, idx, 6, &m);
        } else {
            const m3_vertex q[4] = { NV(-1, -1, 0, 1), NV(1, -1, 1, 1), NV(1, 1, 1, 0), NV(-1, 1, 0, 0) };
            submit(r, t, q, 4, idx, 6, &m);
        }
#undef NV
        break;
    }
    case MII3D_ICON_GLASSES: {
        if (t->rgba)
            m3_clear(t, 0x00000000u);
        if (mii->glass_type == 0)
            break;
        const int index = mii3d_res_texture_count(r->res, MII3D_TEX_GLASS) >= 20
            ? mii->glass_type : mii_glass_type_to_ver3(mii->glass_type);
        const mii3d_image_t *tex = get_tex(r, SLOT_GLASS, index);
        if (!tex)
            break;
        m3_material m;
        memset(&m, 0, sizeof(m));
        m.tex = tex;
        m.mode = M3_MOD_ALPHA_LUM;
        common(m.c[0], mii->glass_color);
        m.mirror_u = 1;
#define GV(X, Y, U, V) { .x = X, .y = Y, .z = 0, .w = 1, .u = U, .v = V, .hl = 1, .add = { 0, 0, 0 }, .rim = { 0, 0, 0 } }
        /* both lenses: the texture mirrored across a 2:1 quad */
        const m3_vertex q[4] = { GV(-0.9f, -0.45f, 0, 1), GV(0.9f, -0.45f, 2, 1), GV(0.9f, 0.45f, 2, 0), GV(-0.9f, 0.45f, 0, 0) };
#undef GV
        static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
        submit(r, t, q, 4, idx, 6, &m);
        break;
    }
    default:
        return MII3D_ERR_ARGUMENT;
    }
    return MII3D_OK;
}

mii3d_result_t mii3d_part_icon_finish(mii3d_icon_part_t part, uint8_t *composed, int res, uint8_t *rgba, int size)
{
    if (!composed || !rgba || res <= 0 || size <= 0)
        return MII3D_ERR_ARGUMENT;
    if (part == MII3D_ICON_EYE || part == MII3D_ICON_EYEBROW) {
        /* the face is centered: drop the left half, keeping one part */
        for (int y = 0; y < res; y++)
            memset(composed + 4 * (size_t)y * res, 0, (size_t)res / 2 * 4);
    }
    /* frames measured over all types (see crop_to_icon) */
    static const float extent[] = {
        [MII3D_ICON_EYE] = 70, [MII3D_ICON_EYEBROW] = 68, [MII3D_ICON_MOUTH] = 84,
        [MII3D_ICON_MUSTACHE] = 76, [MII3D_ICON_GLASSES] = 0, [MII3D_ICON_NOSE] = 300,
    };
    crop_to_icon(composed, res, rgba, size, (size_t)part < sizeof(extent) / sizeof(extent[0]) ? extent[part] : 0.0f);
    return MII3D_OK;
}

mii3d_result_t mii3d_part_icon_draws(mii3d_renderer_t *r, const mii_charinfo_t *mii, mii3d_icon_part_t part,
                                     int res, mii3d_draw_fn fn, void *user)
{
    if (!r || !mii || !fn || res <= 0)
        return MII3D_ERR_ARGUMENT;
    m3_target t = { res, res, NULL, NULL };
    r->draw_fn = fn;
    r->draw_user = user;
    mii3d_result_t result = part_icon_compose(r, mii, part, &t);
    r->draw_fn = NULL;
    r->draw_user = NULL;
    return result;
}

mii3d_result_t mii3d_render_part_icon(mii3d_renderer_t *r, const mii_charinfo_t *mii, mii3d_icon_part_t part,
                                      uint8_t *rgba, int size)
{
    if (!r || !mii || !rgba || size <= 0)
        return MII3D_ERR_ARGUMENT;
    const int RES = mii3d_part_icon_res(size);
    uint8_t *tmp = (uint8_t *)malloc((size_t)RES * RES * 4);
    if (!tmp)
        return MII3D_ERR_MEMORY;
    m3_target t = { RES, RES, tmp, NULL };
    mii3d_result_t result = part_icon_compose(r, mii, part, &t);
    if (result == MII3D_OK)
        result = mii3d_part_icon_finish(part, tmp, RES, rgba, size);
    free(tmp);
    return result;
}
