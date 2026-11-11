/* Untiling of Wii U (GX2 / R7xx "Latte") textures, for the textures inside
 * FFLRes*.dat.
 *
 * Every non-trivial texture in those files is stored with GX2's default tile
 * mode resolved to 2D_TILED_THIN1 (macro tiling) on a GPU with 2 pipes,
 * 4 banks and a 256-byte pipe interleave, swizzle 0, one sample, one slice.
 * Only the base mip level is needed. This file implements exactly that
 * case, written from the publicly documented R6xx/R7xx tiling scheme:
 *
 *  - an 8x8-pixel micro tile stores its pixels in a bpp-dependent order;
 *  - a 32x16 macro tile spreads its 8 micro tiles over the 2 pipes x 4 banks
 *    (pipe = x/8 ^ y/8, bank bits = (x/8 ^ y/32), (x/16 ^ y/16));
 *  - addresses interleave 256-byte groups across pipes, then banks.
 */
#include "mii3d_internal.h"

static unsigned pixel_index_thin(unsigned x, unsigned y, unsigned bpp_bits)
{
    unsigned b0, b1, b2, b3, b4, b5;
    switch (bpp_bits) {
    case 8:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = (x >> 2) & 1;
        b3 = (y >> 1) & 1; b4 = y & 1; b5 = (y >> 2) & 1;
        break;
    case 16:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = (x >> 2) & 1;
        b3 = y & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    case 64:
        b0 = x & 1; b1 = y & 1; b2 = (x >> 1) & 1;
        b3 = (x >> 2) & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    case 32:
    default:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = y & 1;
        b3 = (x >> 2) & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    }
    return b0 | (b1 << 1) | (b2 << 2) | (b3 << 3) | (b4 << 4) | (b5 << 5);
}

#define NUM_PIPES   2u
#define NUM_BANKS   4u
#define GROUP_BYTES 256u  /* pipe interleave */
#define MACRO_W     (8u * NUM_BANKS)   /* 32 */
#define MACRO_H     (8u * NUM_PIPES)   /* 16 */

static uint32_t addr_2d_thin1(unsigned x, unsigned y, unsigned bpp_bits, unsigned pitch)
{
    const unsigned elem = (bpp_bits * pixel_index_thin(x, y, bpp_bits)) / 8u;
    const unsigned pipe = ((x >> 3) ^ (y >> 3)) & 1u;
    const unsigned bank = (((y >> 5) ^ (x >> 3)) & 1u) | ((((y >> 4) ^ (x >> 4)) & 1u) << 1);
    const unsigned macro_bytes = (bpp_bits * MACRO_H * MACRO_W) / 8u;
    const unsigned macro_per_row = pitch / MACRO_W;
    const uint32_t macro_ofs = ((x / MACRO_W) + macro_per_row * (y / MACRO_H)) * macro_bytes;
    /* each (pipe, bank) channel holds 1/8 of a macro tile */
    const uint32_t total = elem + (macro_ofs >> 3);
    const uint32_t high = (total & ~(GROUP_BYTES - 1u)) << 3;  /* << (bank bits + pipe bits) */
    const uint32_t low = total & (GROUP_BYTES - 1u);
    return (bank << 9) | (pipe << 8) | low | high;
}

unsigned mii3d_gx2_pitch(unsigned width, unsigned bytes_per_pixel)
{
    /* pitch alignment for 2D_TILED_THIN1: a macro tile row must cover one
     * pipe-interleave group per bank (128, 64, 32 px for 1, 2, 4 bytes) */
    unsigned align = MACRO_W;
    unsigned group_px = (GROUP_BYTES * NUM_BANKS) / (bytes_per_pixel * 8u);
    if (group_px > align)
        align = group_px;
    return (width + align - 1) / align * align;
}

size_t mii3d_gx2_image_size(unsigned width, unsigned height, unsigned bytes_per_pixel)
{
    unsigned h = (height + MACRO_H - 1) / MACRO_H * MACRO_H;
    return (size_t)mii3d_gx2_pitch(width, bytes_per_pixel) * h * bytes_per_pixel;
}

/* 1D_TILED_THIN1: micro tiles laid out row by row. GX2 falls back to it
 * for surfaces smaller than a macro tile (the 8x8 "none" placeholders). */
static unsigned pitch_1d(unsigned width, unsigned bytes_per_pixel)
{
    unsigned align = GROUP_BYTES / (8u * bytes_per_pixel);
    if (align < 8u)
        align = 8u;
    return (width + align - 1) / align * align;
}

static uint32_t addr_1d_thin1(unsigned x, unsigned y, unsigned bpp_bits, unsigned pitch)
{
    const unsigned micro_bytes = bpp_bits * 64u / 8u;
    const unsigned elem = (bpp_bits * pixel_index_thin(x, y, bpp_bits)) / 8u;
    return ((y / 8u) * (pitch / 8u) + x / 8u) * micro_bytes + elem;
}

/* GX2's default tile mode resolves to 1D tiling when the surface is smaller
 * than one macro tile, and to 2D macro tiling otherwise. */
static int uses_macro_tiling(unsigned width, unsigned height, unsigned bytes_per_pixel)
{
    return width >= MACRO_W && height >= MACRO_H &&
           mii3d_gx2_pitch(width, bytes_per_pixel) >= MACRO_W;
}

int mii3d_gx2_untile(uint8_t *dst, const uint8_t *src, size_t src_size,
                     unsigned width, unsigned height, unsigned bytes_per_pixel)
{
    const unsigned bits = bytes_per_pixel * 8u;
    const int macro = uses_macro_tiling(width, height, bytes_per_pixel);
    const unsigned pitch = macro ? mii3d_gx2_pitch(width, bytes_per_pixel) : pitch_1d(width, bytes_per_pixel);
    const size_t need = macro ? mii3d_gx2_image_size(width, height, bytes_per_pixel)
                              : (size_t)pitch * ((height + 7u) / 8u * 8u) * bytes_per_pixel;
    if (need > src_size)
        return -1;
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            uint32_t a = macro ? addr_2d_thin1(x, y, bits, pitch) : addr_1d_thin1(x, y, bits, pitch);
            memcpy(dst + ((size_t)y * width + x) * bytes_per_pixel, src + a, bytes_per_pixel);
        }
    }
    return 0;
}

/* Inverse of mii3d_gx2_untile (used by tests to build tiled data). */
int mii3d_gx2_tile(uint8_t *dst, size_t dst_size, const uint8_t *src,
                   unsigned width, unsigned height, unsigned bytes_per_pixel)
{
    const unsigned bits = bytes_per_pixel * 8u;
    const int macro = uses_macro_tiling(width, height, bytes_per_pixel);
    const unsigned pitch = macro ? mii3d_gx2_pitch(width, bytes_per_pixel) : pitch_1d(width, bytes_per_pixel);
    const size_t need = macro ? mii3d_gx2_image_size(width, height, bytes_per_pixel)
                              : (size_t)pitch * ((height + 7u) / 8u * 8u) * bytes_per_pixel;
    if (need > dst_size)
        return -1;
    memset(dst, 0, need);
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
            memcpy(dst + (macro ? addr_2d_thin1(x, y, bits, pitch) : addr_1d_thin1(x, y, bits, pitch)),
                   src + ((size_t)y * width + x) * bytes_per_pixel, bytes_per_pixel);
    return (int)need;
}
