/* A small software rasterizer: perspective-correct textured triangles,
 * depth test, alpha blending, the color "modulation" modes Mii parts use,
 * and the Switch Mii lighting model. Targets and textures are row 0 = top. */
#ifndef MII3D_RASTER_H
#define MII3D_RASTER_H

#include "mii3d_internal.h"

typedef struct m3_target {
    int width, height;
    uint8_t *rgba;  /* width * height * 4 */
    float *depth;   /* width * height, or NULL for 2D passes */
} m3_target;

/* Lighting is evaluated per vertex (it varies smoothly) and combined with
 * the per-pixel albedo in linear space:
 *   color = albedo * hl + add * k + rim
 * where k is 1, or (1 - alpha * blend) for the textured faceline. */
typedef struct m3_vertex {
    float x, y, z, w;  /* clip space */
    float u, v;        /* texture coordinates (v = 0 at the top row) */
    float hl;          /* half-Lambert diffuse factor */
    float add[3];      /* specular + subsurface, linear */
    float rim[3];      /* rim light, linear */
} m3_vertex;

typedef enum m3_modulate {
    M3_MOD_NONE,        /* untextured: base color only */
    M3_MOD_DIRECT,      /* texture as is (RGBA) */
    M3_MOD_LAYERED_RGB, /* r*c0 + g*c1 + b*c2, alpha = a */
    M3_MOD_ALPHA,       /* c0 with alpha = r */
    M3_MOD_LUM_ALPHA,   /* r*c0 with alpha = g */
    M3_MOD_LUM,         /* r*c0, opaque */
    M3_MOD_ALPHA_LUM    /* g*c0 with alpha = r (glasses) */
} m3_modulate;

typedef struct m3_material {
    const mii3d_image_t *tex;   /* NULL for untextured */
    m3_modulate mode;
    float base[4];              /* color under the texture (alpha 0 = none) */
    float c[3][4];              /* modulation colors, sRGB 0..1 */
    int mirror_u;               /* mirrored repeat in u (glasses) */
    int depth_test, depth_write;
    float depth_bias;           /* subtracted from depth before testing */
    int lit;                    /* apply vertex lighting (3D passes) */
    int faceline;               /* opaque; texture alpha masks add (see above) */
    float sss_blend;            /* the material's SSS/specular blend factor */
    /* blend over the target's color even where its alpha is 0, and let
     * alpha only accumulate coverage (faceline layers over the skin) */
    int mix_rgb;
    /* skip triangles facing away: +1 or -1 picks which winding is the
     * front (mirrored placements flip it), 0 draws both sides */
    int cull;
} m3_material;

void m3_clear(m3_target *t, uint32_t argb);
void m3_draw(m3_target *t, const m3_vertex *verts, const uint16_t *indices, int index_count,
             const m3_material *mat);

static inline void m3_rgb(float out[4], uint32_t rgb, float alpha)
{
    out[0] = ((rgb >> 16) & 0xFF) / 255.0f;
    out[1] = ((rgb >> 8) & 0xFF) / 255.0f;
    out[2] = (rgb & 0xFF) / 255.0f;
    out[3] = alpha;
}

#endif
