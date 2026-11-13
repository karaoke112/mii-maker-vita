/* Poses: the Mii bodies and skeletal animations of a Wii U game's
 * MiiBodyMiddle.bfres (Mario Kart 8's; the player's own copy, never shipped).
 *
 * BFRES 3.x, big-endian. Read here:
 *  - FMDL "MaleBody" / "FemaleBody": one shape each, rigidly skinned (each
 *    vertex follows one bone; its position is in that bone's space), on a
 *    16-bone skeleton (Euler XYZ rotations; the bind pose is a T-pose);
 *  - FSKA: the animations, curves of each bone's rotation and translation.
 * Nothing in the file is trusted: every offset is checked against its size. */
#include "mii3d_internal.h"

#include <math.h>
#include <stdlib.h>

#define MAX_BONES 24

typedef struct pose_bone {
    char name[16];
    int parent;
    float rot[3], trans[3]; /* bind pose (scale is 1 throughout) */
} pose_bone;

typedef struct pose_body {
    int ok;
    int vertex_count;
    float *pos;    /* xyz, in the bone's space */
    float *nrm;    /* xyz */
    float *uv;     /* uv */
    uint8_t *bone; /* skeleton bone index */
    uint16_t *idx; /* shirt triangles, then pants */
    int shirt_count, pants_count;
    int bone_count;
    pose_bone bones[MAX_BONES];
    int head; /* the head bone */
} pose_body;

typedef struct pose_anim {
    uint32_t ofs;
    int frames;
    const char *name;
} pose_anim;

struct mii3d_poses {
    const uint8_t *d;
    size_t size;
    pose_body body[2]; /* male, female */
    int count;
    pose_anim *anims;
};

/* ---- reading, with bounds -------------------------------------------------- */

typedef struct rd { const uint8_t *d; size_t size; int bad; } rd;

static int in(rd *r, uint32_t o, uint32_t n)
{
    if ((size_t)o + n > r->size || o == 0) {
        r->bad = 1;
        return 0;
    }
    return 1;
}
static uint32_t u32(rd *r, uint32_t o) { return in(r, o, 4) ? m3_be32(r->d + o) : 0; }
static uint16_t u16(rd *r, uint32_t o) { return in(r, o, 2) ? m3_be16(r->d + o) : 0; }
static uint8_t u8(rd *r, uint32_t o) { return in(r, o, 1) ? r->d[o] : 0; }
static float f32(rd *r, uint32_t o) { return in(r, o, 4) ? m3_bef(r->d + o) : 0.0f; }
/* a self-relative offset; 0 when null or out of the file */
static uint32_t rel(rd *r, uint32_t o)
{
    if (!in(r, o, 4))
        return 0;
    const int32_t v = (int32_t)m3_be32(r->d + o);
    if (v == 0)
        return 0;
    const int64_t t = (int64_t)o + v;
    if (t <= 0 || (size_t)t >= r->size) {
        r->bad = 1;
        return 0;
    }
    return (uint32_t)t;
}
/* a name: a relative offset to a NUL-terminated string in the file */
static const char *name_at(rd *r, uint32_t o)
{
    const uint32_t p = rel(r, o);
    if (!p)
        return "";
    if (!memchr(r->d + p, 0, r->size - p)) {
        r->bad = 1;
        return "";
    }
    return (const char *)r->d + p;
}

/* An index group: `count` entries after the root, each with a name and data. */
static int group_count(rd *r, uint32_t g)
{
    const int n = g ? (int)u32(r, g + 4) : 0;
    return n < 0 || n > 4096 ? 0 : n;
}
static uint32_t group_data(rd *r, uint32_t g, int i, const char **name)
{
    const uint32_t e = g + 8 + 16 * (uint32_t)(i + 1);
    if (name)
        *name = name_at(r, e + 8);
    return rel(r, e + 12);
}

static float half(uint16_t h)
{
    const int s = h >> 15, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0)
        v = ldexpf((float)m, -24);
    else if (e == 31)
        v = 65504.0f;
    else
        v = ldexpf((float)(m | 1024), e - 25);
    return s ? -v : v;
}

/* ---- bodies ----------------------------------------------------------------- */

static void body_free(pose_body *b)
{
    free(b->pos);
    free(b->nrm);
    free(b->uv);
    free(b->bone);
    free(b->idx);
    memset(b, 0, sizeof(*b));
}

/* one attribute of vertex i: its data's address, or 0 */
typedef struct attr { uint32_t base, stride, fmt; } attr;

static int load_body(rd *r, uint32_t m, pose_body *b)
{
    memset(b, 0, sizeof(*b));
    /* skeleton */
    const uint32_t skl = rel(r, m + 0x0C);
    if (!skl || memcmp(r->d + skl, "FSKL", 4) != 0)
        return 0;
    const int nb = u16(r, skl + 8);
    const int nmtx = u16(r, skl + 0x0A) + u16(r, skl + 0x0C);
    const uint32_t bones = rel(r, skl + 0x14), mtab = rel(r, skl + 0x18);
    if (nb <= 0 || nb > MAX_BONES || !bones || !mtab || nmtx <= 0 || nmtx > 64)
        return 0;
    b->bone_count = nb;
    b->head = -1;
    for (int i = 0; i < nb; i++) {
        const uint32_t o = bones + 0x40 * (uint32_t)i;
        pose_bone *pb = &b->bones[i];
        strncpy(pb->name, name_at(r, o), sizeof(pb->name) - 1);
        pb->parent = (int16_t)u16(r, o + 6);
        if (pb->parent >= i)
            return 0; /* parents come first */
        for (int k = 0; k < 3; k++) {
            pb->rot[k] = f32(r, o + 0x20 + 4 * k);
            pb->trans[k] = f32(r, o + 0x30 + 4 * k);
        }
        if (strcmp(pb->name, "head") == 0)
            b->head = i;
    }
    if (b->head < 0)
        return 0;
    uint8_t palette[64];
    for (int i = 0; i < nmtx; i++) {
        const int bi = u16(r, mtab + 2 * (uint32_t)i);
        if (bi >= nb)
            return 0;
        palette[i] = (uint8_t)bi;
    }

    /* the shape and its vertex buffer */
    const uint32_t shapes = rel(r, m + 0x14);
    if (group_count(r, shapes) < 1)
        return 0;
    const uint32_t shp = group_data(r, shapes, 0, NULL);
    const uint32_t vtx = rel(r, m + 0x10) + 0x20 * (uint32_t)u16(r, shp + 0x12);
    if (!shp || !vtx || memcmp(r->d + vtx, "FVTX", 4) != 0)
        return 0;
    const int nattr = u8(r, vtx + 4), nbuf = u8(r, vtx + 5);
    const int nv = (int)u32(r, vtx + 8);
    if (nv <= 0 || nv > 20000 || nbuf > 16)
        return 0;
    const uint32_t attrs = rel(r, vtx + 0x10), bufs = rel(r, vtx + 0x18);
    attr p = { 0, 0, 0 }, n = { 0, 0, 0 }, t = { 0, 0, 0 }, ix = { 0, 0, 0 };
    for (int a = 0; a < nattr; a++) {
        const uint32_t ao = attrs + 12 * (uint32_t)a;
        const char *an = name_at(r, ao);
        const int bi = u8(r, ao + 4);
        if (bi >= nbuf)
            return 0;
        const uint32_t bo = bufs + 0x18 * (uint32_t)bi;
        attr at;
        at.base = rel(r, bo + 0x14) + u16(r, ao + 6);
        at.stride = u16(r, bo + 0x0C);
        at.fmt = u32(r, ao + 8);
        if (!in(r, at.base, at.stride * (uint32_t)nv))
            return 0;
        if (strcmp(an, "_p0") == 0) p = at;
        else if (strcmp(an, "_n0") == 0) n = at;
        else if (strcmp(an, "_u0") == 0) t = at;
        else if (strcmp(an, "_i0") == 0) ix = at;
    }
    if (p.fmt != 0x80F || n.fmt != 0x20B || t.fmt != 0x007 || ix.fmt != 0x100)
        return 0; /* the formats MiiBodyMiddle uses */

    b->vertex_count = nv;
    b->pos = (float *)malloc(sizeof(float) * 3 * (size_t)nv);
    b->nrm = (float *)malloc(sizeof(float) * 3 * (size_t)nv);
    b->uv = (float *)malloc(sizeof(float) * 2 * (size_t)nv);
    b->bone = (uint8_t *)malloc((size_t)nv);
    if (!b->pos || !b->nrm || !b->uv || !b->bone)
        return 0;
    for (int i = 0; i < nv; i++) {
        const uint8_t *pp = r->d + p.base + p.stride * (uint32_t)i;
        for (int k = 0; k < 3; k++)
            b->pos[3 * i + k] = half(m3_be16(pp + 2 * k));
        const uint32_t w = m3_be32(r->d + n.base + n.stride * (uint32_t)i);
        for (int k = 0; k < 3; k++) {
            int x = (int)((w >> (10 * k)) & 0x3FF);
            if (x & 0x200)
                x -= 0x400;
            b->nrm[3 * i + k] = (float)x / 511.0f;
        }
        const uint8_t *tp = r->d + t.base + t.stride * (uint32_t)i;
        b->uv[2 * i] = m3_be16(tp) / 65535.0f;
        b->uv[2 * i + 1] = m3_be16(tp + 2) / 65535.0f;
        const int pi = r->d[ix.base + ix.stride * (uint32_t)i];
        if (pi >= nmtx)
            return 0;
        b->bone[i] = palette[pi];
    }

    /* triangles, shirt first: the top half of the body texture is the
     * favorite color (shirt, sleeves, hands; a dress), the bottom the pants */
    const uint32_t lod = rel(r, shp + 0x24);
    if (!lod || u32(r, lod) != 4 /* triangles */)
        return 0;
    const uint32_t fmt = u32(r, lod + 4), count = u32(r, lod + 8), skip = u32(r, lod + 0x18);
    const uint32_t ib = rel(r, lod + 0x14), idata = ib ? rel(r, ib + 0x14) : 0;
    if ((fmt != 4 && fmt != 0) || count % 3 || count > 60000 || !in(r, idata, count * 2))
        return 0;
    b->idx = (uint16_t *)malloc(sizeof(uint16_t) * count);
    if (!b->idx)
        return 0;
    int head = 0, tail = (int)count;
    for (uint32_t k = 0; k < count; k += 3) {
        uint16_t tri[3];
        float v = 0;
        for (int j = 0; j < 3; j++) {
            const uint32_t q = idata + 2 * (k + (uint32_t)j);
            const uint32_t vi = (fmt == 4 ? m3_be16(r->d + q) : (uint16_t)(r->d[q] | r->d[q + 1] << 8)) + skip;
            if (vi >= (uint32_t)nv)
                return 0;
            tri[j] = (uint16_t)vi;
            v += b->uv[2 * vi + 1];
        }
        uint16_t *dst = v < 1.5f ? b->idx + head : b->idx + (tail -= 3);
        memcpy(dst, tri, sizeof(tri));
        if (v < 1.5f)
            head += 3;
    }
    b->shirt_count = head;
    b->pants_count = (int)count - head;
    b->ok = 1;
    return 1;
}

/* ---- the file ----------------------------------------------------------------- */

mii3d_result_t mii3d_poses_open(mii3d_poses_t **out, const uint8_t *file, size_t size)
{
    if (!out || !file)
        return MII3D_ERR_ARGUMENT;
    *out = NULL;
    if (size < 0x6C || memcmp(file, "FRES", 4) != 0 || m3_be16(file + 8) != 0xFEFF)
        return MII3D_ERR_FORMAT;
    mii3d_poses_t *ps = (mii3d_poses_t *)calloc(1, sizeof(*ps));
    if (!ps)
        return MII3D_ERR_MEMORY;
    ps->d = file;
    ps->size = size;
    rd r = { file, size, 0 };

    const uint32_t models = rel(&r, 0x20);
    for (int i = 0; i < group_count(&r, models); i++) {
        const char *name;
        const uint32_t m = group_data(&r, models, i, &name);
        const int g = strcmp(name, "MaleBody") == 0 ? 0 : strcmp(name, "FemaleBody") == 0 ? 1 : -1;
        if (g < 0 || !m || ps->body[g].ok)
            continue;
        if (!load_body(&r, m, &ps->body[g]))
            body_free(&ps->body[g]);
    }
    const uint32_t anims = rel(&r, 0x28);
    const int n = group_count(&r, anims);
    ps->anims = (pose_anim *)calloc((size_t)(n > 0 ? n : 1), sizeof(pose_anim));
    if (!ps->anims) {
        mii3d_poses_close(ps);
        return MII3D_ERR_MEMORY;
    }
    for (int i = 0; i < n; i++) {
        const char *name;
        const uint32_t a = group_data(&r, anims, i, &name);
        if (!a || !in(&r, a, 0x30) || memcmp(file + a, "FSKA", 4) != 0)
            continue;
        const int frames = (int)u32(&r, a + 0x10);
        if (frames <= 0 || frames > 100000)
            continue;
        pose_anim *pa = &ps->anims[ps->count++];
        pa->ofs = a;
        pa->frames = frames;
        pa->name = name;
    }
    if (r.bad || !ps->body[0].ok || !ps->body[1].ok || ps->count == 0) {
        mii3d_poses_close(ps);
        return MII3D_ERR_FORMAT;
    }
    *out = ps;
    return MII3D_OK;
}

void mii3d_poses_close(mii3d_poses_t *ps)
{
    if (!ps)
        return;
    body_free(&ps->body[0]);
    body_free(&ps->body[1]);
    free(ps->anims);
    free(ps);
}

int mii3d_poses_count(const mii3d_poses_t *ps) { return ps ? ps->count : 0; }

const char *mii3d_poses_name(const mii3d_poses_t *ps, int index)
{
    return ps && index >= 0 && index < ps->count ? ps->anims[index].name : "";
}

int mii3d_poses_frames(const mii3d_poses_t *ps, int index)
{
    return ps && index >= 0 && index < ps->count ? ps->anims[index].frames : 0;
}

/* ---- evaluating a frame -------------------------------------------------------- */

/* One curve's value at `frame`: cubic (four coefficients per key), linear
 * (two) or stepped/baked (one), each key scaled and the first offset. */
static int curve_eval(rd *r, uint32_t q, float frame, uint32_t *target, float *value)
{
    const uint16_t flags = u16(r, q);
    const int n = u16(r, q + 2);
    *target = u32(r, q + 4);
    const float scale = f32(r, q + 0x10), offset = f32(r, q + 0x14);
    const uint32_t fo = rel(r, q + 0x1C), ko = rel(r, q + 0x20);
    const int ft = flags & 3, kt = (flags >> 2) & 3, ct = (flags >> 4) & 7;
    const int per = ct == 0 ? 4 : ct == 1 ? 2 : 1;
    const int fsz = ft == 0 ? 4 : ft == 1 ? 2 : 1, ksz = kt == 0 ? 4 : kt == 1 ? 2 : 1;
    if (n <= 0 || !in(r, fo, (uint32_t)(n * fsz)) || !in(r, ko, (uint32_t)(n * per * ksz)))
        return 0;
#define FRAME(i) (ft == 0 ? m3_bef(r->d + fo + 4 * (uint32_t)(i)) \
                 : ft == 1 ? (int16_t)m3_be16(r->d + fo + 2 * (uint32_t)(i)) / 32.0f : (float)r->d[fo + (uint32_t)(i)])
#define KEY(i) (kt == 0 ? m3_bef(r->d + ko + 4 * (uint32_t)(i)) \
               : kt == 1 ? (float)(int16_t)m3_be16(r->d + ko + 2 * (uint32_t)(i)) : (float)(int8_t)r->d[ko + (uint32_t)(i)])
    int seg = 0;
    while (seg + 1 < n - 1 && FRAME(seg + 1) <= frame)
        seg++;
    float t = 0.0f;
    if (n > 1) {
        const float f0 = FRAME(seg), f1 = FRAME(seg + 1);
        t = f1 > f0 ? (frame - f0) / (f1 - f0) : 0.0f;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
    }
    const int k = seg * per;
    float v = KEY(k) * scale + offset;
    if (ct == 0)
        v += (KEY(k + 1) + (KEY(k + 2) + KEY(k + 3) * t) * t) * t * scale;
    else if (ct == 1)
        v += KEY(k + 1) * t * scale;
#undef FRAME
#undef KEY
    *value = v;
    return 1;
}

/* rotation (Euler XYZ, as Rz * Ry * Rx) and translation as a 3x4 matrix */
static void srt(const float rot[3], const float tr[3], float m[12])
{
    const float cx = cosf(rot[0]), sx = sinf(rot[0]), cy = cosf(rot[1]), sy = sinf(rot[1]);
    const float cz = cosf(rot[2]), sz = sinf(rot[2]);
    m[0] = cy * cz; m[1] = sx * sy * cz - cx * sz; m[2] = cx * sy * cz + sx * sz; m[3] = tr[0];
    m[4] = cy * sz; m[5] = sx * sy * sz + cx * cz; m[6] = cx * sy * sz - sx * cz; m[7] = tr[1];
    m[8] = -sy;     m[9] = sx * cy;                m[10] = cx * cy;               m[11] = tr[2];
}

static void mul34(const float a[12], const float b[12], float out[12])
{
    float o[12];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            o[4 * r + c] = a[4 * r] * b[c] + a[4 * r + 1] * b[4 + c] + a[4 * r + 2] * b[8 + c] + (c == 3 ? a[4 * r + 3] : 0.0f);
        }
    }
    memcpy(out, o, sizeof(o));
}

/* Every bone's model matrix at `frame` of animation `index` (-1: the bind
 * pose). `frame` < 0: the middle of the animation. */
static void pose_bones(const mii3d_poses_t *ps, const pose_body *b, int index, float frame, float world[][12])
{
    float rot[MAX_BONES][3], tr[MAX_BONES][3];
    for (int i = 0; i < b->bone_count; i++) {
        memcpy(rot[i], b->bones[i].rot, sizeof(rot[i]));
        memcpy(tr[i], b->bones[i].trans, sizeof(tr[i]));
    }
    if (index >= 0 && index < ps->count) {
        rd r = { ps->d, ps->size, 0 };
        const uint32_t a = ps->anims[index].ofs;
        if (frame < 0)
            frame = ps->anims[index].frames * 0.5f;
        const int nba = u16(&r, a + 0x14);
        const uint32_t ba = rel(&r, a + 0x20);
        for (int i = 0; i < nba && ba && !r.bad; i++) {
            const uint32_t o = ba + 0x18 * (uint32_t)i;
            const char *name = name_at(&r, o + 4);
            int bi = -1;
            for (int k = 0; k < b->bone_count; k++)
                if (strcmp(b->bones[k].name, name) == 0)
                    bi = k;
            if (bi < 0)
                continue;
            /* the base values: scale (unused), rotation, translation */
            const uint32_t flags = u32(&r, o);
            const uint32_t base = rel(&r, o + 0x14);
            uint32_t p = base;
            if (base) {
                if (flags & 0x08)
                    p += 12;
                if (flags & 0x10) {
                    for (int k = 0; k < 3; k++)
                        rot[bi][k] = f32(&r, p + 4 * (uint32_t)k);
                    p += 16;
                }
                if (flags & 0x20)
                    for (int k = 0; k < 3; k++)
                        tr[bi][k] = f32(&r, p + 4 * (uint32_t)k);
            }
            const int nc = u8(&r, o + 0x0A);
            const uint32_t curves = rel(&r, o + 0x10);
            for (int c = 0; c < nc && curves; c++) {
                uint32_t target;
                float v;
                if (!curve_eval(&r, curves + 0x24 * (uint32_t)c, frame, &target, &v))
                    continue;
                if (target >= 0x10 && target <= 0x18)
                    tr[bi][(target - 0x10) / 4] = v;
                else if (target >= 0x20 && target <= 0x28)
                    rot[bi][(target - 0x20) / 4] = v;
            }
        }
    }
    for (int i = 0; i < b->bone_count; i++) {
        float local[12];
        srt(rot[i], tr[i], local);
        if (b->bones[i].parent >= 0)
            mul34(world[b->bones[i].parent], local, world[i]);
        else
            memcpy(world[i], local, sizeof(local));
    }
}

int mii3d_poses_build(const mii3d_poses_t *ps, int gender, int index, float frame, float scale,
                      mii3d_mesh_t *mesh, float head[12], int *shirt_count)
{
    if (!ps || !mesh)
        return 0;
    const pose_body *b = &ps->body[gender ? 1 : 0];
    float world[MAX_BONES][12], bind[MAX_BONES][12];
    pose_bones(ps, b, -1, 0, bind);
    pose_bones(ps, b, index, frame, world);
    /* MiiBodyMiddle's units to the body's: its neck where FFLBodyRes's is */
    const float k = scale / bind[b->head][7];
    if (mesh->vertex_count < b->vertex_count) {
        float *p = (float *)realloc(mesh->positions, sizeof(float) * 3 * (size_t)b->vertex_count);
        if (p)
            mesh->positions = p;
        float *n = (float *)realloc(mesh->normals, sizeof(float) * 3 * (size_t)b->vertex_count);
        if (n)
            mesh->normals = n;
        if (!p || !n)
            return 0;
    }
    mesh->vertex_count = b->vertex_count;
    mesh->uvs = b->uv;
    mesh->params = NULL;
    mesh->indices = b->idx;
    mesh->index_count = b->shirt_count + b->pants_count;
    for (int i = 0; i < b->vertex_count; i++) {
        const float *m = world[b->bone[i]], *p = b->pos + 3 * i, *n = b->nrm + 3 * i;
        for (int r = 0; r < 3; r++) {
            mesh->positions[3 * i + r] = (m[4 * r] * p[0] + m[4 * r + 1] * p[1] + m[4 * r + 2] * p[2] + m[4 * r + 3]) * k;
            mesh->normals[3 * i + r] = m[4 * r] * n[0] + m[4 * r + 1] * n[1] + m[4 * r + 2] * n[2];
        }
    }
    if (head) {
        memcpy(head, world[b->head], sizeof(float) * 12);
        for (int r = 0; r < 3; r++)
            head[4 * r + 3] *= k;
    }
    if (shirt_count)
        *shirt_count = b->shirt_count;
    return 1;
}

float mii3d_poses_neck(const mii3d_poses_t *ps, int gender, int index, float frame)
{
    if (!ps || index < 0 || index >= ps->count)
        return 1.0f;
    const pose_body *b = &ps->body[gender ? 1 : 0];
    float world[MAX_BONES][12], bind[MAX_BONES][12];
    pose_bones(ps, b, -1, 0, bind);
    pose_bones(ps, b, index, frame, world);
    return bind[b->head][7] > 0 ? world[b->head][7] / bind[b->head][7] : 1.0f;
}
