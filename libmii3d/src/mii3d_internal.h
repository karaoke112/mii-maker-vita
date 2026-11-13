#ifndef MII3D_INTERNAL_H
#define MII3D_INTERNAL_H

#include "mii3d/mii3d.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

unsigned mii3d_gx2_pitch(unsigned width, unsigned bytes_per_pixel);
size_t   mii3d_gx2_image_size(unsigned width, unsigned height, unsigned bytes_per_pixel);
int      mii3d_gx2_untile(uint8_t *dst, const uint8_t *src, size_t src_size,
                          unsigned width, unsigned height, unsigned bytes_per_pixel);
int      mii3d_gx2_tile(uint8_t *dst, size_t dst_size, const uint8_t *src,
                        unsigned width, unsigned height, unsigned bytes_per_pixel);

static inline uint32_t m3_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint16_t m3_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline float m3_bef(const uint8_t *p)
{
    uint32_t u = m3_be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* pose.c: the body of `gender` posed at a frame of animation `index` (-1:
 * the bind pose), scaled so its neck is at height `scale`: positions and
 * normals into `mesh` (reallocated as needed; its uvs and indices point
 * into the poses, shirt triangles first), and the head bone's matrix (3x4,
 * row-major, translation in the same units). */
int mii3d_poses_build(const mii3d_poses_t *ps, int gender, int index, float frame, float scale,
                      mii3d_mesh_t *mesh, float head[12], int *shirt_count);

#endif
