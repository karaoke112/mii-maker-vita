/* Reader for the Wii U face resource format ("FFRA", e.g. FFLResHigh.dat).
 *
 * Layout (big-endian), as documented by the FFL decompilation's headers and
 * confirmed against real files:
 *
 *   0x0000  "FFRA", version 0x00070000, uncompressed buffer size,
 *           expanded size, is-expanded flag                          (0x14)
 *   0x0014  texture header: 11 max sizes + 317 part infos            (0x13FC)
 *   0x1410  shape header:   12 max sizes + 857 part infos            (0x35C0)
 *   0x49D0  unused                                                   (0x30)
 *
 *   part info (0x10): data offset, size, compressed size,
 *                     level, window bits, memory level, strategy
 *
 * Each part is a zlib stream (or stored raw when strategy == 5).
 * A texture ends with a 12-byte footer: mip offset, width, height,
 * mip count, format (0 = R8, 1 = RG8, 2 = RGBA8), and is GX2-tiled.
 * A shape starts with a 0x90-byte header: offsets[6], sizes[6] of
 * position (f32 x4), normal (10:10:10:2 snorm), texcoord (f32 x2),
 * tangent, color, index (u16; its "size" is the index count),
 * then a bounding box and a 0x48-byte transform block.
 */
#include "mii3d_internal.h"

#include <stdlib.h>
#include <zlib.h>

#define TEX_HEADER_OFS   0x14u

/* Texture table sizes differ between variants; the shape table follows it.
 * Wii U files (FFLRes*.dat) store GX2-tiled textures; Miitomo files
 * (AFLResHigh*.dat) store them linearly with more eyes, eyebrows, mouths
 * and (in 2_3) the Switch's 20 glasses. */
typedef struct layout {
    mii3d_variant_t variant;
    int tex_counts[MII3D_TEX_KIND_COUNT];
    int linear;
} layout;

static const layout k_layouts[] = {
    { MII3D_VARIANT_WIIU,          { 3, 132, 62, 24, 12, 12, 9, 2, 37, 6, 18 }, 0 },
    { MII3D_VARIANT_MIITOMO_2_3,   { 3, 132, 80, 28, 12, 12, 20, 2, 52, 6, 18 }, 1 },
    { MII3D_VARIANT_MIITOMO,       { 3, 132, 80, 28, 12, 12, 9, 2, 52, 6, 18 }, 1 },
};

static const int k_shape_counts[MII3D_SHAPE_KIND_COUNT] = { 4, 132, 132, 12, 1, 12, 18, 18, 132, 132, 132, 132 };

typedef struct part_info {
    uint32_t pos, size, csize;
    uint8_t window_bits, strategy;
} part_info;

struct mii3d_res {
    const uint8_t *file;
    size_t size;
    const layout *layout;
    const uint8_t *tex_info[MII3D_TEX_KIND_COUNT];
    const uint8_t *shape_info[MII3D_SHAPE_KIND_COUNT];
};

const char *mii3d_result_string(mii3d_result_t r)
{
    switch (r) {
    case MII3D_OK:           return "ok";
    case MII3D_ERR_ARGUMENT: return "invalid argument";
    case MII3D_ERR_FORMAT:   return "not a supported FFRA resource file";
    case MII3D_ERR_RANGE:    return "part not present";
    case MII3D_ERR_DATA:     return "corrupt part data";
    case MII3D_ERR_MEMORY:   return "out of memory";
    }
    return "unknown error";
}

int mii3d_res_texture_count(const mii3d_res_t *res, mii3d_tex_kind_t kind)
{
    return res && (unsigned)kind < MII3D_TEX_KIND_COUNT ? res->layout->tex_counts[kind] : 0;
}

mii3d_variant_t mii3d_res_variant(const mii3d_res_t *res)
{
    return res ? res->layout->variant : MII3D_VARIANT_WIIU;
}

int mii3d_res_shape_count(mii3d_shape_kind_t kind)
{
    return (unsigned)kind < MII3D_SHAPE_KIND_COUNT ? k_shape_counts[kind] : 0;
}

static int apply_layout(mii3d_res_t *res, const layout *l)
{
    size_t ofs = TEX_HEADER_OFS + 4 * MII3D_TEX_KIND_COUNT;
    res->layout = l;
    for (int k = 0; k < MII3D_TEX_KIND_COUNT; k++) {
        res->tex_info[k] = res->file + ofs;
        ofs += 16 * (size_t)l->tex_counts[k];
    }
    ofs += 4 * MII3D_SHAPE_KIND_COUNT; /* shape max sizes */
    for (int k = 0; k < MII3D_SHAPE_KIND_COUNT; k++) {
        res->shape_info[k] = res->file + ofs;
        ofs += 16 * (size_t)k_shape_counts[k];
    }
    return ofs + 0x30 <= res->size;
}

/* A wrong layout can still point at real (but different) parts, so a
 * layout is only accepted if typical parts decode as what they should be. */
static int layout_works(const mii3d_res_t *res)
{
    for (int k = 0; k < MII3D_SHAPE_KIND_COUNT; k++)
        for (int i = 0; i < k_shape_counts[k]; i++) {
            const uint8_t *p = res->shape_info[k] + 16 * i;
            if ((uint64_t)m3_be32(p) + m3_be32(p + 8) > res->size)
                return 0;
        }
    mii3d_mesh_t m;
    if (mii3d_res_shape(res, MII3D_SHAPE_FACELINE, 0, &m) != MII3D_OK)
        return 0;
    int ok = m.vertex_count > 0 && m.uvs != NULL && m.bbox_max[1] > m.bbox_min[1];
    mii3d_mesh_free(&m);
    if (!ok || mii3d_res_shape(res, MII3D_SHAPE_MASK, 0, &m) != MII3D_OK)
        return 0;
    mii3d_mesh_free(&m);
    mii3d_image_t img;
    if (mii3d_res_texture(res, MII3D_TEX_EYE, 1, &img) != MII3D_OK)
        return 0;
    ok = img.channels == 4;
    mii3d_image_free(&img);
    if (!ok || mii3d_res_texture(res, MII3D_TEX_NOSELINE, 1, &img) != MII3D_OK)
        return 0;
    ok = img.channels == 1;
    mii3d_image_free(&img);
    return ok;
}

mii3d_result_t mii3d_res_open(mii3d_res_t **out, const uint8_t *file, size_t size)
{
    mii3d_res_t *res;

    if (!out || !file)
        return MII3D_ERR_ARGUMENT;
    *out = NULL;
    if (size < 0x14 || memcmp(file, "FFRA", 4) != 0 || m3_be32(file + 4) != 0x00070000u)
        return MII3D_ERR_FORMAT;
    res = (mii3d_res_t *)calloc(1, sizeof(*res));
    if (!res)
        return MII3D_ERR_MEMORY;
    res->file = file;
    res->size = size;
    for (size_t i = 0; i < sizeof(k_layouts) / sizeof(k_layouts[0]); i++) {
        if (apply_layout(res, &k_layouts[i]) && layout_works(res)) {
            *out = res;
            return MII3D_OK;
        }
    }
    free(res);
    return MII3D_ERR_FORMAT;
}

/* FFLBodyRes.dat (from Nintendo Web Framework titles) uses the Wii U layout
 * with no textures; the two static body meshes sit in the nose shape slots
 * 3 and 4 (male, female). */
#define BODY_SLOT 3

static int body_works(const mii3d_res_t *res)
{
    for (int g = 0; g < 2; g++) {
        mii3d_mesh_t m;
        if (mii3d_res_shape(res, MII3D_SHAPE_NOSE, BODY_SLOT + g, &m) != MII3D_OK)
            return 0;
        const int ok = m.vertex_count > 100 && m.bbox_max[1] - m.bbox_min[1] > 40.0f;
        mii3d_mesh_free(&m);
        if (!ok)
            return 0;
    }
    return 1;
}

mii3d_result_t mii3d_res_open_body(mii3d_res_t **out, const uint8_t *file, size_t size)
{
    mii3d_res_t *res;

    if (!out || !file)
        return MII3D_ERR_ARGUMENT;
    *out = NULL;
    if (size < 0x14 || memcmp(file, "FFRA", 4) != 0 || m3_be32(file + 4) != 0x00070000u)
        return MII3D_ERR_FORMAT;
    res = (mii3d_res_t *)calloc(1, sizeof(*res));
    if (!res)
        return MII3D_ERR_MEMORY;
    res->file = file;
    res->size = size;
    if (apply_layout(res, &k_layouts[0]) && body_works(res)) {
        *out = res;
        return MII3D_OK;
    }
    free(res);
    return MII3D_ERR_FORMAT;
}

mii3d_result_t mii3d_res_body(const mii3d_res_t *res, int gender, mii3d_mesh_t *out)
{
    if (!res || !out || gender < 0 || gender > 1)
        return MII3D_ERR_ARGUMENT;
    return mii3d_res_shape(res, MII3D_SHAPE_NOSE, BODY_SLOT + gender, out);
}

void mii3d_res_close(mii3d_res_t *res)
{
    free(res);
}

static part_info read_info(const uint8_t *p)
{
    part_info i;
    i.pos = m3_be32(p);
    i.size = m3_be32(p + 4);
    i.csize = m3_be32(p + 8);
    i.window_bits = p[13];
    i.strategy = p[15];
    return i;
}

/* Returns a malloc'd buffer of info.size bytes, or NULL. */
static uint8_t *load_part(const mii3d_res_t *res, part_info info, mii3d_result_t *err)
{
    uint8_t *buf;
    if (info.size == 0) {
        *err = MII3D_ERR_RANGE;
        return NULL;
    }
    if ((size_t)info.pos + info.csize > res->size) {
        *err = MII3D_ERR_DATA;
        return NULL;
    }
    buf = (uint8_t *)malloc(info.size);
    if (!buf) {
        *err = MII3D_ERR_MEMORY;
        return NULL;
    }
    if (info.strategy == 5) { /* stored */
        if (info.csize < info.size) {
            free(buf);
            *err = MII3D_ERR_DATA;
            return NULL;
        }
        memcpy(buf, res->file + info.pos, info.size);
        return buf;
    }
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)(res->file + info.pos);
    zs.avail_in = info.csize;
    zs.next_out = buf;
    zs.avail_out = info.size;
    /* 15 accepts any smaller window recorded in the stream header */
    if (inflateInit2(&zs, 15) != Z_OK) {
        free(buf);
        *err = MII3D_ERR_MEMORY;
        return NULL;
    }
    int zr = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zr != Z_STREAM_END || zs.total_out != info.size) {
        free(buf);
        *err = MII3D_ERR_DATA;
        return NULL;
    }
    return buf;
}

mii3d_result_t mii3d_res_texture(const mii3d_res_t *res, mii3d_tex_kind_t kind, int index, mii3d_image_t *out)
{
    mii3d_result_t err = MII3D_OK;
    uint8_t *raw;
    part_info info;
    static const uint8_t k_channels[3] = { 1, 2, 4 };

    if (!res || !out || (unsigned)kind >= MII3D_TEX_KIND_COUNT)
        return MII3D_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= res->layout->tex_counts[kind])
        return MII3D_ERR_RANGE;
    info = read_info(res->tex_info[kind] + 16 * index);
    raw = load_part(res, info, &err);
    if (!raw)
        return err;
    if (info.size < 12) {
        free(raw);
        return MII3D_ERR_DATA;
    }
    const uint8_t *footer = raw + info.size - 12;
    const unsigned w = m3_be16(footer + 4), h = m3_be16(footer + 6), fmt = footer[9];
    if (fmt > 2 || w == 0 || h == 0) {
        free(raw);
        return MII3D_ERR_DATA;
    }
    out->width = (uint16_t)w;
    out->height = (uint16_t)h;
    out->channels = k_channels[fmt];
    out->pixels = (uint8_t *)malloc((size_t)w * h * out->channels);
    {
        static uint32_t next_serial;
#if defined(__GNUC__)
        out->serial = __atomic_add_fetch(&next_serial, 1, __ATOMIC_RELAXED);
#else
        out->serial = ++next_serial;
#endif
    }
    if (!out->pixels) {
        free(raw);
        return MII3D_ERR_MEMORY;
    }
    /* the base level comes first; mips (ignored) follow */
    if (res->layout->linear) {
        const size_t n = (size_t)w * h * out->channels;
        if (n > info.size - 12) {
            free(raw);
            mii3d_image_free(out);
            return MII3D_ERR_DATA;
        }
        memcpy(out->pixels, raw, n);
    } else if (mii3d_gx2_untile(out->pixels, raw, info.size - 12, w, h, out->channels) != 0) {
        free(raw);
        mii3d_image_free(out);
        return MII3D_ERR_DATA;
    }
    free(raw);
    return MII3D_OK;
}

void mii3d_image_free(mii3d_image_t *img)
{
    if (img) {
        free(img->pixels);
        memset(img, 0, sizeof(*img));
    }
}

enum { EL_POSITION, EL_NORMAL, EL_TEXCOORD, EL_TANGENT, EL_COLOR, EL_INDEX };

mii3d_result_t mii3d_res_shape(const mii3d_res_t *res, mii3d_shape_kind_t kind, int index, mii3d_mesh_t *out)
{
    mii3d_result_t err = MII3D_OK;
    uint8_t *raw;
    part_info info;
    uint32_t pos[6], size[6];

    if (!res || !out || (unsigned)kind >= MII3D_SHAPE_KIND_COUNT)
        return MII3D_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= k_shape_counts[kind])
        return MII3D_ERR_RANGE;
    info = read_info(res->shape_info[kind] + 16 * index);
    raw = load_part(res, info, &err);
    if (!raw)
        return err;
    /* 0x48 bytes of offsets, sizes and bounding box; face shapes add a
     * transform block (to 0x90), body shapes do not */
    if (info.size < (kind == MII3D_SHAPE_FACELINE ? 0x90u : 0x48u)) {
        free(raw);
        return MII3D_ERR_DATA;
    }
    for (int i = 0; i < 6; i++) {
        pos[i] = m3_be32(raw + 4 * i);
        size[i] = m3_be32(raw + 24 + 4 * i);
    }
    for (int i = 0; i < 3; i++) {
        out->bbox_min[i] = m3_bef(raw + 48 + 4 * i);
        out->bbox_max[i] = m3_bef(raw + 60 + 4 * i);
    }
    if (kind == MII3D_SHAPE_FACELINE) {
        for (int i = 0; i < 3; i++) {
            out->hair_translate[i] = m3_bef(raw + 72 + 4 * i);
            out->nose_translate[i] = m3_bef(raw + 84 + 4 * i);
            out->beard_translate[i] = m3_bef(raw + 96 + 4 * i);
        }
    }

    const int nv = (int)(size[EL_POSITION] / 16u);
    const int ni = (int)size[EL_INDEX];
    if ((size_t)pos[EL_POSITION] + (size_t)nv * 16 > info.size ||
        (size_t)pos[EL_INDEX] + (size_t)ni * 2 > info.size ||
        (size[EL_NORMAL] && (size_t)pos[EL_NORMAL] + (size_t)nv * 4 > info.size) ||
        (size[EL_TEXCOORD] && (size_t)pos[EL_TEXCOORD] + (size_t)nv * 8 > info.size) ||
        ni % 3 != 0) {
        free(raw);
        return MII3D_ERR_DATA;
    }

    out->vertex_count = nv;
    out->index_count = ni;
    out->positions = (float *)malloc(sizeof(float) * 3 * (size_t)(nv ? nv : 1));
    out->normals = (float *)calloc((size_t)(nv ? nv : 1) * 3, sizeof(float));
    out->uvs = size[EL_TEXCOORD] ? (float *)malloc(sizeof(float) * 2 * (size_t)nv) : NULL;
    out->indices = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)(ni ? ni : 1));
    /* per-vertex parameters (hair: specular blend) when present */
    const int has_params = nv > 0 && size[EL_COLOR] == (uint32_t)nv * 4u &&
                           (size_t)pos[EL_COLOR] + (size_t)nv * 4 <= info.size;
    out->params = has_params ? (uint8_t *)malloc((size_t)nv) : NULL;
    if (!out->positions || !out->normals || !out->indices || (size[EL_TEXCOORD] && !out->uvs) ||
        (has_params && !out->params)) {
        free(raw);
        mii3d_mesh_free(out);
        return MII3D_ERR_MEMORY;
    }
    for (int v = 0; v < nv; v++) {
        const uint8_t *p = raw + pos[EL_POSITION] + 16 * v;
        out->positions[3 * v + 0] = m3_bef(p);
        out->positions[3 * v + 1] = m3_bef(p + 4);
        out->positions[3 * v + 2] = m3_bef(p + 8);
        if (size[EL_NORMAL]) {
            uint32_t n = m3_be32(raw + pos[EL_NORMAL] + 4 * v);
            int32_t nx = (int32_t)(n << 22) >> 22, ny = (int32_t)(n << 12) >> 22, nz = (int32_t)(n << 2) >> 22;
            out->normals[3 * v + 0] = (float)nx / 511.0f;
            out->normals[3 * v + 1] = (float)ny / 511.0f;
            out->normals[3 * v + 2] = (float)nz / 511.0f;
        }
        if (out->params)
            out->params[v] = raw[pos[EL_COLOR] + 4 * v];
        if (out->uvs) {
            const uint8_t *t = raw + pos[EL_TEXCOORD] + 8 * v;
            out->uvs[2 * v + 0] = m3_bef(t);
            out->uvs[2 * v + 1] = m3_bef(t + 4);
        }
    }
    for (int i = 0; i < ni; i++) {
        uint16_t idx = m3_be16(raw + pos[EL_INDEX] + 2 * i);
        if (idx >= nv) {
            free(raw);
            mii3d_mesh_free(out);
            return MII3D_ERR_DATA;
        }
        out->indices[i] = idx;
    }
    free(raw);
    return MII3D_OK;
}

void mii3d_mesh_free(mii3d_mesh_t *mesh)
{
    if (mesh) {
        free(mesh->positions);
        free(mesh->normals);
        free(mesh->uvs);
        free(mesh->params);
        free(mesh->indices);
        memset(mesh, 0, sizeof(*mesh));
    }
}
