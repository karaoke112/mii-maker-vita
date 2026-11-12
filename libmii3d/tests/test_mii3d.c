/* libmii3d tests. They use only synthetic data; Nintendo's resource files
 * are not part of the repository.
 *
 *   test_mii3d                  run the tests
 *   MII3D_RES=FFLResHigh.dat test_mii3d   also smoke-test a real resource
 */
#include "../src/mii3d_internal.h"
#include "../src/raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

static int g_failed, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_failed++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- GX2 tiling --------------------------------------------------------- */

/* CRC32 of a synthetic pattern after tiling with GX2's own address library
 * (addrlib, via a separate tool), so these pin our addressing to the
 * hardware layout. */
static const struct { unsigned w, h, bpp; unsigned size; uint32_t crc; } k_tile_vectors[] = {
    { 144, 128, 1, 32768, 0x9ccf672fu },
    { 96, 192, 1, 24576, 0x9e7bd7f4u },
    { 128, 128, 2, 32768, 0x03de72c8u },
    { 76, 64, 4, 24576, 0xfcf21643u },
    { 256, 512, 1, 131072, 0x8a8ee402u },
    { 32, 32, 1, 4096, 0xf83be0a8u },
    { 8, 8, 1, 256, 0x20ac217au },
    { 8, 8, 4, 256, 0x496b30d4u },
};

static void test_tiling(void)
{
    for (size_t i = 0; i < sizeof(k_tile_vectors) / sizeof(k_tile_vectors[0]); i++) {
        const unsigned w = k_tile_vectors[i].w, h = k_tile_vectors[i].h, bpp = k_tile_vectors[i].bpp;
        uint8_t *lin = malloc((size_t)w * h * bpp), *back = malloc((size_t)w * h * bpp);
        uint8_t *til = malloc(k_tile_vectors[i].size);
        for (unsigned y = 0; y < h; y++)
            for (unsigned x = 0; x < w; x++)
                for (unsigned b = 0; b < bpp; b++)
                    lin[((size_t)y * w + x) * bpp + b] = (uint8_t)(x * 7 + y * 13 + b * 101 + (x * y) % 251);
        int n = mii3d_gx2_tile(til, k_tile_vectors[i].size, lin, w, h, bpp);
        CHECK(n == (int)k_tile_vectors[i].size);
        CHECK(crc32(0, til, k_tile_vectors[i].size) == k_tile_vectors[i].crc);
        CHECK(mii3d_gx2_untile(back, til, k_tile_vectors[i].size, w, h, bpp) == 0);
        CHECK(memcmp(lin, back, (size_t)w * h * bpp) == 0);
        CHECK(mii3d_gx2_untile(back, til, k_tile_vectors[i].size - 1, w, h, bpp) != 0);
        free(lin);
        free(back);
        free(til);
    }
}

/* ---- synthetic FFRA resource -------------------------------------------- */

static void wbe32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static void wbe16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void wbef(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); wbe32(p, u); }

typedef struct builder { uint8_t *buf; size_t size, cap; } builder;

static size_t append(builder *b, const uint8_t *data, size_t n)
{
    if (b->size + n > b->cap) {
        b->cap = (b->size + n) * 2;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->size, data, n);
    b->size += n;
    return b->size - n;
}

static size_t info_offset_tex(int kind, int index)
{
    static const int counts[] = { 3, 132, 62, 24, 12, 12, 9, 2, 37, 6, 18 };
    size_t o = 0x14 + 44;
    for (int k = 0; k < kind; k++) o += 16 * counts[k];
    return o + 16 * index;
}

static size_t info_offset_shape(int kind, int index)
{
    static const int counts[] = { 4, 132, 132, 12, 1, 12, 18, 18, 132, 132, 132, 132 };
    size_t o = 0x1410 + 48;
    for (int k = 0; k < kind; k++) o += 16 * counts[k];
    return o + 16 * index;
}

static void add_part(builder *b, size_t info_ofs, const uint8_t *data, size_t n)
{
    uLongf cn = compressBound(n);
    uint8_t *c = malloc(cn);
    compress2(c, &cn, data, n, 6);
    size_t pos = append(b, c, cn);
    uint8_t *info = b->buf + info_ofs;
    wbe32(info, (uint32_t)pos);
    wbe32(info + 4, (uint32_t)n);
    wbe32(info + 8, (uint32_t)cn);
    info[12] = 6; info[13] = 7; info[14] = 8; info[15] = 0;
    free(c);
}

/* A square in the z = 25 plane, 60 x 60, centered at y = 30, facing +z. */
static size_t make_square_shape(uint8_t *out, int faceline)
{
    const float pos[4][3] = { { -30, 0, 25 }, { 30, 0, 25 }, { 30, 60, 25 }, { -30, 60, 25 } };
    const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
    const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    const size_t p_ofs = 0x90, n_ofs = p_ofs + 64, t_ofs = n_ofs + 16, i_ofs = t_ofs + 32, end = i_ofs + 12;
    memset(out, 0, end);
    wbe32(out + 0, (uint32_t)p_ofs); wbe32(out + 24, 64);
    wbe32(out + 4, (uint32_t)n_ofs); wbe32(out + 28, 16);
    wbe32(out + 8, (uint32_t)t_ofs); wbe32(out + 32, 32);
    wbe32(out + 20, (uint32_t)i_ofs); wbe32(out + 44, 6);
    wbef(out + 48, -30); wbef(out + 52, 0); wbef(out + 56, 25);
    wbef(out + 60, 30); wbef(out + 64, 60); wbef(out + 68, 25);
    if (faceline) {
        wbef(out + 72 + 4, 3.0f);   /* hair translate y */
        wbef(out + 84 + 4, 24.5f);  /* nose translate y */
        wbef(out + 84 + 8, 26.0f);
    }
    for (int v = 0; v < 4; v++) {
        for (int k = 0; k < 3; k++) wbef(out + p_ofs + 16 * v + 4 * k, pos[v][k]);
        wbef(out + p_ofs + 16 * v + 12, 1.0f);
        wbe32(out + n_ofs + 4 * v, 511u << 20); /* normal (0, 0, 1) */
        wbef(out + t_ofs + 8 * v, uv[v][0]);
        wbef(out + t_ofs + 8 * v + 4, uv[v][1]);
    }
    for (int i = 0; i < 6; i++) wbe16(out + i_ofs + 2 * i, idx[i]);
    return end;
}

/* RGBA8 76x64 eye-like texture: left half red channel, right half blue. */
static size_t make_texture(uint8_t *out)
{
    const unsigned w = 76, h = 64;
    uint8_t lin[76 * 64 * 4];
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            uint8_t *p = lin + 4 * (y * w + x);
            p[0] = x < w / 2 ? 255 : 0; p[1] = 0; p[2] = x < w / 2 ? 0 : 255; p[3] = 255;
        }
    int n = mii3d_gx2_tile(out, 1 << 16, lin, w, h, 4);
    uint8_t *f = out + n;
    wbe32(f, 0); wbe16(f + 4, (uint16_t)w); wbe16(f + 6, (uint16_t)h); f[8] = 1; f[9] = 2; f[10] = f[11] = 0;
    return (size_t)n + 12;
}

/* R8 96x96 noseline-like texture. */
static size_t make_r8_texture(uint8_t *out)
{
    const unsigned w = 96, h = 96;
    static uint8_t lin[96 * 96];
    for (unsigned i = 0; i < w * h; i++)
        lin[i] = (uint8_t)i;
    int n = mii3d_gx2_tile(out, 1 << 16, lin, w, h, 1);
    uint8_t *f = out + n;
    wbe32(f, 0); wbe16(f + 4, (uint16_t)w); wbe16(f + 6, (uint16_t)h); f[8] = 1; f[9] = 0; f[10] = f[11] = 0;
    return (size_t)n + 12;
}

static builder make_resource(void)
{
    builder b = { 0 };
    uint8_t header[0x4A00] = { 0 };
    memcpy(header, "FFRA", 4);
    wbe32(header + 4, 0x00070000u);
    append(&b, header, sizeof(header));
    static uint8_t tmp[1 << 16];
    size_t n = make_square_shape(tmp, 1);
    add_part(&b, info_offset_shape(MII3D_SHAPE_FACELINE, 0), tmp, n);
    n = make_square_shape(tmp, 0);
    add_part(&b, info_offset_shape(MII3D_SHAPE_MASK, 0), tmp, n);
    n = make_texture(tmp);
    add_part(&b, info_offset_tex(MII3D_TEX_EYE, 2), tmp, n);
    add_part(&b, info_offset_tex(MII3D_TEX_EYE, 1), tmp, n);
    n = make_r8_texture(tmp);
    add_part(&b, info_offset_tex(MII3D_TEX_NOSELINE, 1), tmp, n);
    return b;
}

static void test_resource(void)
{
    builder b = make_resource();
    mii3d_res_t *res;
    CHECK(mii3d_res_open(&res, b.buf, 100) == MII3D_ERR_FORMAT);
    CHECK(mii3d_res_open(&res, b.buf, b.size) == MII3D_OK);
    CHECK(mii3d_res_variant(res) == MII3D_VARIANT_WIIU);
    CHECK(mii3d_res_texture_count(res, MII3D_TEX_GLASS) == 9);

    mii3d_mesh_t m;
    CHECK(mii3d_res_shape(res, MII3D_SHAPE_FACELINE, 0, &m) == MII3D_OK);
    CHECK(m.vertex_count == 4 && m.index_count == 6);
    CHECK(m.positions[3] == 30.0f && m.positions[7] == 60.0f);
    CHECK(m.normals[2] > 0.99f && m.normals[0] == 0.0f);
    CHECK(m.uvs && m.uvs[1] == 1.0f);
    CHECK(m.hair_translate[1] == 3.0f && m.nose_translate[2] == 26.0f);
    mii3d_mesh_free(&m);
    CHECK(mii3d_res_shape(res, MII3D_SHAPE_FACELINE, 1, &m) == MII3D_ERR_RANGE);
    CHECK(mii3d_res_shape(res, MII3D_SHAPE_FACELINE, 99, &m) == MII3D_ERR_RANGE);

    mii3d_image_t img;
    CHECK(mii3d_res_texture(res, MII3D_TEX_EYE, 2, &img) == MII3D_OK);
    CHECK(img.width == 76 && img.height == 64 && img.channels == 4);
    CHECK(img.pixels[0] == 255 && img.pixels[2] == 0);
    CHECK(img.pixels[4 * 75] == 0 && img.pixels[4 * 75 + 2] == 255);
    mii3d_image_free(&img);

    /* corrupt compressed data is reported, not crashed on */
    uint32_t pos = m3_be32(b.buf + info_offset_tex(MII3D_TEX_EYE, 2));
    b.buf[pos + 10] ^= 0xFF;
    CHECK(mii3d_res_texture(res, MII3D_TEX_EYE, 2, &img) == MII3D_ERR_DATA);
    b.buf[pos + 10] ^= 0xFF;

    /* render: the faceline square covers the middle of the image */
    mii3d_renderer_t *r;
    CHECK(mii3d_renderer_create(&r, res) == MII3D_OK);
    mii_charinfo_t ci;
    mii_charinfo_init_default(&ci, 0);
    ci.faceline_type = 0;
    ci.faceline_color = 0;
    ci.eye_type = 2;
    uint8_t *px = calloc(128 * 128, 4);
    mii3d_view_t view;
    mii3d_view_init_default(&view);
    CHECK(mii3d_render_head(r, &ci, &view, px, 128, 128) == MII3D_OK);
    const uint8_t *center = px + 4 * (64 * 128 + 64), *corner = px;
    CHECK(center[3] == 255);
    CHECK(center[0] > 150 && center[1] > 120); /* lit skin */
    CHECK(corner[3] == 0);                     /* transparent background */
    /* part icons: eyes come out centered and non-empty, glasses type 0 empty */
    {
        uint8_t icon[48 * 48 * 4];
        CHECK(mii3d_render_part_icon(r, &ci, MII3D_ICON_EYES, icon, 48) == MII3D_OK);
        int covered = 0;
        for (int i = 0; i < 48 * 48; i++) covered += icon[4 * i + 3] > 0;
        CHECK(covered > 48 * 48 / 4);
        CHECK(icon[3] == 0); /* margin stays transparent */
        /* one eye: still fills the icon (it is scaled up on its own) */
        CHECK(mii3d_render_part_icon(r, &ci, MII3D_ICON_EYE, icon, 48) == MII3D_OK);
        covered = 0;
        for (int i = 0; i < 48 * 48; i++) covered += icon[4 * i + 3] > 0;
        CHECK(covered > 48 * 48 / 8);
        ci.glass_type = 0;
        CHECK(mii3d_render_part_icon(r, &ci, MII3D_ICON_GLASSES, icon, 48) == MII3D_OK);
        covered = 0;
        for (int i = 0; i < 48 * 48; i++) covered += icon[4 * i + 3] > 0;
        CHECK(covered == 0);
        CHECK(mii3d_render_part_icon(r, &ci, (mii3d_icon_part_t)99, icon, 48) == MII3D_ERR_ARGUMENT);
    }
    /* deterministic output */
    uint8_t *px2 = calloc(128 * 128, 4);
    CHECK(mii3d_render_head(r, &ci, &view, px2, 128, 128) == MII3D_OK);
    CHECK(memcmp(px, px2, 128 * 128 * 4) == 0);
    free(px);
    free(px2);
    mii3d_renderer_destroy(r);
    mii3d_res_close(res);
    free(b.buf);
}

/* ---- rasterizer --------------------------------------------------------- */

static void test_raster(void)
{
    uint8_t rgba[32 * 32 * 4];
    float depth[32 * 32];
    m3_target t = { 32, 32, rgba, depth };
    m3_clear(&t, 0x00000000u);
    m3_vertex v[3] = {
        { -1, -1, 0.5f, 1, 0, 0, 1, 0 }, { 1, -1, 0.5f, 1, 0, 0, 1, 0 }, { -1, 1, 0.5f, 1, 0, 0, 1, 0 },
    };
    const uint16_t idx[3] = { 0, 1, 2 };
    m3_material m;
    memset(&m, 0, sizeof(m));
    m3_rgb(m.base, 0xFF0000, 1.0f);
    m.depth_test = m.depth_write = 1;
    m3_draw(&t, v, idx, 3, &m);
    CHECK(rgba[4 * (31 * 32 + 0)] == 255 && rgba[4 * (31 * 32 + 0) + 3] == 255); /* bottom-left inside */
    CHECK(rgba[4 * (0 * 32 + 31) + 3] == 0);                                    /* top-right outside */
    /* a farther triangle does not overwrite */
    for (int i = 0; i < 3; i++) v[i].z = 0.9f;
    m3_rgb(m.base, 0x0000FF, 1.0f);
    m3_draw(&t, v, idx, 3, &m);
    CHECK(rgba[4 * (31 * 32 + 0)] == 255 && rgba[4 * (31 * 32 + 0) + 2] == 0);
}

/* ---- optional: real resource -------------------------------------------- */

static void test_real_resource(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "MII3D_RES: cannot open %s\n", path);
        g_failed++;
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    CHECK(fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    mii3d_res_t *res;
    CHECK(mii3d_res_open(&res, buf, (size_t)n) == MII3D_OK);
    mii3d_renderer_t *r;
    mii3d_renderer_create(&r, res);
    uint8_t *px = malloc(128 * 128 * 4);
    for (int g = 0; g < 2; g++) {
        mii_charinfo_t ci;
        mii_charinfo_init_default(&ci, g);
        CHECK(mii3d_render_head(r, &ci, NULL, px, 128, 128) == MII3D_OK);
        int covered = 0;
        for (int i = 0; i < 128 * 128; i++) covered += px[4 * i + 3] > 0;
        CHECK(covered > 128 * 128 / 4 && covered < 128 * 128 * 9 / 10);
    }
    printf("real resource %s: ok (variant %d)\n", path, (int)mii3d_res_variant(res));
    free(px);
    mii3d_renderer_destroy(r);
    mii3d_res_close(res);
    free(buf);
}

int main(void)
{
    test_tiling();
    test_resource();
    test_raster();
    const char *res = getenv("MII3D_RES");
    if (res && *res)
        test_real_resource(res);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
