/*
 * libmii3d - draws a Mii's head in 3D from the Wii U face resource
 * (FFLResHigh.dat / FFLResMiddle.dat, which users dump from their own
 * console; it is not distributed with this project).
 *
 * Everything runs on the CPU: a small software rasterizer produces an RGBA
 * image that any UI (NanoVG, GXM, GL, a PNG writer) can display. A head is
 * a few thousand triangles, so this is fast enough on a PS Vita for an
 * editor that re-renders when something changes.
 */
#ifndef MII3D_MII3D_H
#define MII3D_MII3D_H

#include <stddef.h>
#include <stdint.h>

#include <mii/mii.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mii3d_result {
    MII3D_OK = 0,
    MII3D_ERR_ARGUMENT = -1,
    MII3D_ERR_FORMAT = -2,   /* not an FFRA resource, or unsupported version */
    MII3D_ERR_RANGE = -3,    /* part index out of range or a part is missing */
    MII3D_ERR_DATA = -4,     /* corrupt compressed data */
    MII3D_ERR_MEMORY = -5
} mii3d_result_t;

const char *mii3d_result_string(mii3d_result_t r);

/* ---- resource file ------------------------------------------------------ */

typedef struct mii3d_res mii3d_res_t;

/* `file` must stay valid until mii3d_res_close(). */
/* Accepts the Wii U files (FFLResHigh.dat, FFLResMiddle.dat and their LG
 * variants) and Miitomo's AFLResHigh_2_3.dat; the layout is detected. */
mii3d_result_t mii3d_res_open(mii3d_res_t **out, const uint8_t *file, size_t size);
void mii3d_res_close(mii3d_res_t *res);

/* The body resource, FFLBodyRes.dat (found in Wii U titles built on the
 * Nintendo Web Framework): the face-resource container with the two static
 * body meshes (gender 0 = male, 1 = female) and no textures. Close it with
 * mii3d_res_close(). */
mii3d_result_t mii3d_res_open_body(mii3d_res_t **out, const uint8_t *file, size_t size);

typedef enum mii3d_variant {
    MII3D_VARIANT_WIIU,         /* FFLRes*.dat: GX2-tiled textures, 9 glasses */
    MII3D_VARIANT_MIITOMO_2_3,  /* AFLResHigh_2_3.dat: linear, 20 glasses (all Switch styles) */
    MII3D_VARIANT_MIITOMO       /* AFLResHigh.dat: linear, 9 glasses */
} mii3d_variant_t;

mii3d_variant_t mii3d_res_variant(const mii3d_res_t *res);

typedef enum mii3d_tex_kind {
    MII3D_TEX_BEARD, MII3D_TEX_CAP, MII3D_TEX_EYE, MII3D_TEX_EYEBROW,
    MII3D_TEX_FACELINE, MII3D_TEX_MAKEUP, MII3D_TEX_GLASS, MII3D_TEX_MOLE,
    MII3D_TEX_MOUTH, MII3D_TEX_MUSTACHE, MII3D_TEX_NOSELINE,
    MII3D_TEX_KIND_COUNT
} mii3d_tex_kind_t;

typedef enum mii3d_shape_kind {
    MII3D_SHAPE_BEARD, MII3D_SHAPE_HAT_NORMAL, MII3D_SHAPE_HAT_CAP, MII3D_SHAPE_FACELINE,
    MII3D_SHAPE_GLASS, MII3D_SHAPE_MASK, MII3D_SHAPE_NOSELINE, MII3D_SHAPE_NOSE,
    MII3D_SHAPE_HAIR_NORMAL, MII3D_SHAPE_HAIR_CAP, MII3D_SHAPE_FOREHEAD_NORMAL,
    MII3D_SHAPE_FOREHEAD_CAP,
    MII3D_SHAPE_KIND_COUNT
} mii3d_shape_kind_t;

int mii3d_res_texture_count(const mii3d_res_t *res, mii3d_tex_kind_t kind);
int mii3d_res_shape_count(mii3d_shape_kind_t kind);

/* A decoded texture: linear rows, `channels` bytes per pixel (1 = R,
 * 2 = RG, 4 = RGBA), row 0 at the top. Free with mii3d_image_free(). */
typedef struct mii3d_image {
    uint16_t width, height;
    uint8_t channels;
    uint8_t *pixels;
    /* a number unique to each image read from a resource (0 for others):
     * lets a GPU backend cache textures safely */
    uint32_t serial;
} mii3d_image_t;

mii3d_result_t mii3d_res_texture(const mii3d_res_t *res, mii3d_tex_kind_t kind, int index, mii3d_image_t *out);
void mii3d_image_free(mii3d_image_t *img);

/* A decoded mesh (triangle list). `uvs` is NULL when the shape has none.
 * Free with mii3d_mesh_free(). */
typedef struct mii3d_mesh {
    int vertex_count, index_count;
    float *positions; /* xyz */
    float *normals;   /* xyz */
    float *uvs;       /* uv */
    uint8_t *params;  /* per-vertex parameter (hair specular blend), or NULL */
    uint16_t *indices;
    float bbox_min[3], bbox_max[3];
    /* For faceline shapes: where hair, nose and beard attach. */
    float hair_translate[3], nose_translate[3], beard_translate[3];
} mii3d_mesh_t;

mii3d_result_t mii3d_res_shape(const mii3d_res_t *res, mii3d_shape_kind_t kind, int index, mii3d_mesh_t *out);
/* One of the two body meshes of a body resource. */
mii3d_result_t mii3d_res_body(const mii3d_res_t *res, int gender, mii3d_mesh_t *out);
void mii3d_mesh_free(mii3d_mesh_t *mesh);

/* ---- rendering ---------------------------------------------------------- */

typedef struct mii3d_renderer mii3d_renderer_t;

mii3d_result_t mii3d_renderer_create(mii3d_renderer_t **out, const mii3d_res_t *res);
/* Attaches a body resource (from mii3d_res_open_body; NULL detaches it),
 * used by views with full_body set. It must outlive the renderer or be
 * detached first. */
void mii3d_renderer_set_body(mii3d_renderer_t *r, const mii3d_res_t *body);
void mii3d_renderer_destroy(mii3d_renderer_t *r);

/* ---- poses (a Wii U game's MiiBodyMiddle.bfres) -------------------------
 *
 * Mario Kart 8's Mii bodies come rigged, with 171 animations (waving,
 * bowing, dancing, sitting...). With the file attached, a full-body view
 * with view.pose > 0 draws that body posed as a frame of an animation,
 * the head following its head bone. `file` must outlive the poses. */
typedef struct mii3d_poses mii3d_poses_t;
mii3d_result_t mii3d_poses_open(mii3d_poses_t **out, const uint8_t *file, size_t size);
void mii3d_poses_close(mii3d_poses_t *poses);
int mii3d_poses_count(const mii3d_poses_t *poses);
const char *mii3d_poses_name(const mii3d_poses_t *poses, int index); /* e.g. "Handwaving00_o" */
int mii3d_poses_frames(const mii3d_poses_t *poses, int index);
/* Where the neck is in a pose (`frame` < 0: the middle), as a fraction of
 * its standing height: 1 standing, less sitting or lying down. */
float mii3d_poses_neck(const mii3d_poses_t *poses, int gender, int index, float frame);
/* Attaches poses (NULL detaches); they must outlive the renderer or be
 * detached first. */
void mii3d_renderer_set_poses(mii3d_renderer_t *r, const mii3d_poses_t *poses);

typedef struct mii3d_view {
    float yaw_deg;     /* turn the head; 0 faces the camera */
    float pitch_deg;
    uint32_t background; /* 0xAARRGGBB; alpha 0 = transparent */
    int face_texture_size; /* 0 = default (256) */
    int supersample;       /* 1 (fast) to 3; 2 gives smooth edges at 4x the cost */
    float zoom;            /* 1 = whole head; 0 means 1 */
    float center_y;        /* move the framing up (+) or down, in model units */
    int full_body;         /* draw the whole Mii (needs mii3d_renderer_set_body) */
    int expression;        /* FFL's expressions: 0 = normal, below mii3d_expression_count() */
    /* full_body only: 0 = standing (FFLBodyRes's body); n = the pose of
     * animation n - 1 of the file given to mii3d_renderer_set_poses */
    int pose;
    float pose_frame;      /* the frame of that animation; < 0: its middle */
    /* the pants, as nn::mii colors them: MII3D_PANTS_GRAY (normal),
     * _BLUE (a gift), _RED (a favorite), _GOLD (special) */
    int pants;
} mii3d_view_t;

enum { MII3D_PANTS_GRAY = 0, MII3D_PANTS_BLUE = 1, MII3D_PANTS_RED = 2, MII3D_PANTS_GOLD = 3 };

void mii3d_view_init_default(mii3d_view_t *view);

/* How many expressions the resource has the textures for: 19 (FFL's
 * standard set, any FFLRes), 70 with Miitomo's AFLRes (80 eye textures),
 * else 1 (only the normal face). */
int mii3d_expression_count(const mii3d_res_t *res);

/* Draws the head into `rgba` (width * height * 4 bytes, row 0 at top,
 * non-premultiplied). */
mii3d_result_t mii3d_render_head(mii3d_renderer_t *r, const mii_charinfo_t *mii, const mii3d_view_t *view,
                                 uint8_t *rgba, int width, int height);

/* ---- drawing with your own rasterizer (e.g. a GPU) ----------------------
 *
 * mii3d_render_head_draws() does everything mii3d_render_head() does except
 * filling pixels: it hands each mesh to `fn` as transformed vertices and a
 * material, in drawing order, for a GPU (or any rasterizer) to draw. The
 * caller clears the target (and depth) first. Reproducing the image:
 *
 *  - vertices: 13 floats each (sizeof = 52 bytes): clip-space x y z w (y up; z/w is 0 at the
 *    near plane and 1 at the far one), texture u v (v = 0 at the top row),
 *    hl (diffuse factor), add r g b (specular + subsurface, linear), rim
 *    r g b (linear). The lighting terms interpolate linearly.
 *  - fragment color: see mii3d_draw_t's fields; colors and textures are
 *    sRGB, lighting happens in linear space (gamma 2.2) and the result is
 *    converted back to sRGB before blending "over" the target.
 */
typedef enum mii3d_modulate {
    MII3D_MOD_NONE,        /* base color only */
    MII3D_MOD_DIRECT,      /* texture RGBA as is */
    MII3D_MOD_LAYERED_RGB, /* rgb = r*c0 + g*c1 + b*c2, a = texture a */
    MII3D_MOD_ALPHA,       /* rgb = c0, a = texture r */
    MII3D_MOD_LUM_ALPHA,   /* rgb = r*c0, a = texture g */
    MII3D_MOD_LUM,         /* rgb = r*c0, a = 1 */
    MII3D_MOD_ALPHA_LUM    /* rgb = g*c0, a = texture r */
} mii3d_modulate_t;

typedef struct mii3d_draw {
    const float *vertices; /* 13 floats per vertex, see above */
    int vertex_count;
    const uint16_t *indices; /* triangle list */
    int index_count;
    /* 1-, 2- or 4-channel; a channel that is missing reads as 0 (g, b) or 1
     * (a). NULL: untextured. */
    const mii3d_image_t *texture;
    int texture_dynamic; /* the same image object gets new contents (the
                            composed face textures): upload it every time */
    int mirror_u;        /* u repeats mirrored (0..1 then 1..0); otherwise clamp */
    mii3d_modulate_t mode;
    /* shaded = texture over base when base a > 0 (rgb = mix(base, tex, tex a),
     * a = base a + (1 - base a) * tex a), the texture alone otherwise */
    float base[4];
    float c[3][4];
    int lit;      /* rgb = linear(rgb) * hl + add + rim; skip if a <= 0.004 */
    int faceline; /* rgb = linear(rgb) * hl + add * (1 - a * sss_blend) + rim; a = 1 */
    float sss_blend;
    int depth_test;   /* pass when z - depth_bias <= stored depth */
    int depth_write;  /* store z where a > 0.5 */
    float depth_bias;
} mii3d_draw_t;

typedef void (*mii3d_draw_fn)(void *user, const mii3d_draw_t *draw);

mii3d_result_t mii3d_render_head_draws(mii3d_renderer_t *r, const mii_charinfo_t *mii, const mii3d_view_t *view,
                                       int width, int height, mii3d_draw_fn fn, void *user);

/* Icons for the editor's part grids, in the Mii's current colors with the
 * part at its default size and position: the part is drawn alone, cropped
 * and centered in a size x size transparent image. Shape parts (hair, face
 * shape, nose, beard, ...) are drawn as small heads with mii3d_render_head
 * instead. */
typedef enum mii3d_icon_part {
    MII3D_ICON_EYES, MII3D_ICON_EYEBROWS, MII3D_ICON_MOUTH,
    MII3D_ICON_MUSTACHE, MII3D_ICON_GLASSES, MII3D_ICON_MOLE,
    /* one eye / one eyebrow (the one on the image's right), drawn larger */
    MII3D_ICON_EYE, MII3D_ICON_EYEBROW,
    /* the nose's line drawing, in black */
    MII3D_ICON_NOSE
} mii3d_icon_part_t;

mii3d_result_t mii3d_render_part_icon(mii3d_renderer_t *r, const mii_charinfo_t *mii, mii3d_icon_part_t part,
                                      uint8_t *rgba, int size);
/* The same in two steps, for a GPU: mii3d_part_icon_draws() hands the
 * part's draws (2D, no depth) for a res x res transparent target, with res
 * from mii3d_part_icon_res(size); mii3d_part_icon_finish() then crops that
 * picture (read back, not premultiplied; it is modified) into the icon. */
int mii3d_part_icon_res(int size);
mii3d_result_t mii3d_part_icon_draws(mii3d_renderer_t *r, const mii_charinfo_t *mii, mii3d_icon_part_t part,
                                     int res, mii3d_draw_fn fn, void *user);
mii3d_result_t mii3d_part_icon_finish(mii3d_icon_part_t part, uint8_t *composed, int res, uint8_t *rgba, int size);
/* Whether a part type shows its color at all (some mouths are plain lines,
 * the same in every lip color), so icons can be shared. Only mouths are
 * checked; other parts answer 1. */
int mii3d_part_shows_color(mii3d_renderer_t *r, mii3d_icon_part_t part, int type);
/* Whether a hairstyle comes with a hat or hair accessory (drawn in the
 * favorite color). */
int mii3d_hair_has_hat(mii3d_renderer_t *r, int hair_type);

/* Renders only the composited face texture (eyes, brows, mouth, ...) at
 * size x size, mostly for debugging and part thumbnails. */
mii3d_result_t mii3d_render_face_texture(mii3d_renderer_t *r, const mii_charinfo_t *mii,
                                         uint8_t *rgba, int size);

#ifdef __cplusplus
}
#endif

#endif /* MII3D_MII3D_H */
