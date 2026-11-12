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
void mii3d_mesh_free(mii3d_mesh_t *mesh);

/* ---- rendering ---------------------------------------------------------- */

typedef struct mii3d_renderer mii3d_renderer_t;

mii3d_result_t mii3d_renderer_create(mii3d_renderer_t **out, const mii3d_res_t *res);
void mii3d_renderer_destroy(mii3d_renderer_t *r);

typedef struct mii3d_view {
    float yaw_deg;     /* turn the head; 0 faces the camera */
    float pitch_deg;
    uint32_t background; /* 0xAARRGGBB; alpha 0 = transparent */
    int face_texture_size; /* 0 = default (256) */
    int supersample;       /* 1 (fast) to 3; 2 gives smooth edges at 4x the cost */
} mii3d_view_t;

void mii3d_view_init_default(mii3d_view_t *view);

/* Draws the head into `rgba` (width * height * 4 bytes, row 0 at top,
 * non-premultiplied). */
mii3d_result_t mii3d_render_head(mii3d_renderer_t *r, const mii_charinfo_t *mii, const mii3d_view_t *view,
                                 uint8_t *rgba, int width, int height);

/* Renders only the composited face texture (eyes, brows, mouth, ...) at
 * size x size, mostly for debugging and part thumbnails. */
mii3d_result_t mii3d_render_face_texture(mii3d_renderer_t *r, const mii_charinfo_t *mii,
                                         uint8_t *rgba, int size);

#ifdef __cplusplus
}
#endif

#endif /* MII3D_MII3D_H */
