/* libmii unit tests.
 *
 *   test_mii                         run all tests (uses tests/vectors)
 *   test_mii --gen-vectors <dir>     write the synthetic .charinfo inputs
 *   test_mii --ver3-roundtrip f...   Ver3 -> CharInfo -> Ver3 must be exact
 *
 * tests/vectors holds synthetic Miis (made here from a fixed seed) and the
 * .nfcd/.nfsd/.ffsd files MiiJS produced from them, so the encoders are
 * checked against an independent implementation. Regenerate with
 * scripts/gen-test-vectors.sh.
 */
#include "mii/mii.h"
#include "mii/mii_editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed, g_checks;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); g_checks++; \
    if (_a != _b) { g_failed++; fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", \
    __FILE__, __LINE__, #a, _a, _b); } } while (0)

#define VECTOR_COUNT 24

/* ---- helpers ----------------------------------------------------------- */

static uint32_t g_rng = 0x6d696921u;
static uint32_t rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* A random but valid Mii. Uses only values Ver3 can represent when
 * `ver3_safe`, so Ver3 round trips can be exact. */
static void make_random_mii(mii_charinfo_t *ci, int ver3_safe)
{
    static const char *names[] = { "Alex", "Sam", "\xe3\x81\xbf\xe3\x81\xad\xe3\x81\x8a", "Vita Kid", "Ren", "Kai" };
    uint8_t rnd[16];

    memset(ci, 0, sizeof(*ci));
    for (int f = 0; f < MII_FIELD_COUNT; f++) {
        int lo = mii_field_min((mii_field_t)f), hi = mii_field_max((mii_field_t)f);
        mii_field_set(ci, (mii_field_t)f, lo + (int)(rng_next() % (uint32_t)(hi - lo + 1)));
    }
    if (ver3_safe) {
        static const uint8_t hair[8] = { 8, 1, 2, 3, 4, 5, 6, 7 };
        static const uint8_t glass[6] = { 8, 14, 15, 16, 17, 18 };
        ci->hair_color = hair[rng_next() % 8];
        ci->eyebrow_color = hair[rng_next() % 8];
        ci->beard_color = hair[rng_next() % 8];
        ci->eye_color = (uint8_t)(8 + rng_next() % 6);
        ci->mouth_color = (uint8_t)(19 + rng_next() % 5);
        ci->glass_color = glass[rng_next() % 6];
        ci->glass_type = (uint8_t)(rng_next() % 9);
        ci->faceline_color = (uint8_t)(rng_next() % 6);
    }
    mii_name_set_utf8(ci, names[rng_next() % 6]);
    for (int i = 0; i < 16; i++)
        rnd[i] = (uint8_t)rng_next();
    mii_create_id_from_random(&ci->create_id, rnd);
}

static long read_file(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}

/* ---- tests ------------------------------------------------------------- */

static void test_crc(void)
{
    /* CRC-16/XMODEM check value */
    CHECK_EQ(mii_crc16((const uint8_t *)"123456789", 9, 0), 0x31C3);
}

static void test_default(void)
{
    mii_charinfo_t ci;
    char name[32];
    for (int g = 0; g < 2; g++) {
        mii_charinfo_init_default(&ci, g);
        CHECK_EQ(mii_verify(&ci, 0), MII_FIELD_COUNT);
        CHECK_EQ(mii_verify(&ci, 1), MII_FIELD_COUNT + 2); /* no create id yet */
        CHECK_EQ(ci.gender, g);
        mii_name_get_utf8(&ci, name, sizeof(name));
        CHECK(strcmp(name, "no name") == 0);
    }
}

static void test_fields(void)
{
    mii_charinfo_t ci;
    mii_charinfo_init_default(&ci, 0);
    CHECK_EQ(mii_field_set(&ci, MII_FIELD_HAIR_TYPE, 500), 131);
    CHECK_EQ(ci.hair_type, 131);
    CHECK_EQ(mii_field_set(&ci, MII_FIELD_EYEBROW_Y, 0), 3);
    CHECK_EQ(mii_field_get(&ci, MII_FIELD_EYEBROW_Y), 3);
    CHECK_EQ(mii_field_set(&ci, MII_FIELD_MOLE_Y, 30), 30);
    CHECK(strcmp(mii_field_name(MII_FIELD_GLASS_TYPE), "glass_type") == 0);
    ci.eye_type = 60;
    CHECK_EQ(mii_verify(&ci, 0), MII_FIELD_EYE_TYPE);
}

static void test_names(void)
{
    mii_charinfo_t ci;
    char out[64];
    mii_charinfo_init_default(&ci, 0);
    /* Japanese + an emoji (surrogate pair) */
    const char *in = "\xe3\x81\xbf\xe3\x81\xad\xe3\x81\x8a\xf0\x9f\x98\x80";
    CHECK_EQ(mii_name_set_utf8(&ci, in), 5);
    mii_name_get_utf8(&ci, out, sizeof(out));
    CHECK(strcmp(out, in) == 0);
    CHECK_EQ(mii_verify(&ci, 0), MII_FIELD_COUNT);
    /* truncates to 10 units */
    CHECK_EQ(mii_name_set_utf8(&ci, "ABCDEFGHIJKLMN"), 10);
    CHECK_EQ(ci.name[10], 0);
    /* unpaired surrogate is invalid */
    ci.name[0] = 0xD800;
    ci.name[1] = 'a';
    CHECK_EQ(mii_verify(&ci, 0), MII_FIELD_COUNT + 1);
    /* empty is invalid */
    memset(ci.name, 0, sizeof(ci.name));
    CHECK_EQ(mii_verify(&ci, 0), MII_FIELD_COUNT + 1);
}

static void test_create_id(void)
{
    mii_create_id_t id;
    uint8_t zeros[16] = { 0 }, ff[16];
    memset(ff, 0xFF, sizeof(ff));
    mii_create_id_from_random(&id, ff);
    CHECK(mii_create_id_is_valid(&id));
    CHECK_EQ(id.bytes[8] & 0xC0, 0x80);
    CHECK_EQ(id.bytes[6] >> 4, 4);
    memcpy(id.bytes, zeros, 16);
    CHECK(mii_create_id_is_null(&id));
    CHECK(!mii_create_id_is_valid(&id));
}

static int charinfo_equal(const mii_charinfo_t *a, const mii_charinfo_t *b, int with_id)
{
    if (with_id && !mii_create_id_equal(&a->create_id, &b->create_id))
        return 0;
    if (memcmp(a->name, b->name, sizeof(a->name)) != 0)
        return 0;
    for (int f = 0; f < MII_FIELD_COUNT; f++)
        if (mii_field_get(a, (mii_field_t)f) != mii_field_get(b, (mii_field_t)f)) {
            fprintf(stderr, "  field %s: %u != %u\n", mii_field_name((mii_field_t)f),
                    mii_field_get(a, (mii_field_t)f), mii_field_get(b, (mii_field_t)f));
            return 0;
        }
    return 1;
}

static void test_switch_roundtrips(void)
{
    static const uint8_t author[16] = { 'V', 'i', 't', 'a', 'M', 'i', 'i', 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int n = 0; n < 200; n++) {
        mii_charinfo_t a, b;
        uint8_t buf[MII_CHARINFO_SIZE];
        make_random_mii(&a, 0);

        CHECK_EQ(mii_charinfo_encode(&a, buf), MII_OK);
        CHECK_EQ(mii_charinfo_decode(&b, buf, MII_CHARINFO_SIZE), MII_OK);
        CHECK(charinfo_equal(&a, &b, 1));

        CHECK_EQ(mii_coredata_encode(&a, buf), MII_OK);
        CHECK_EQ(mii_coredata_decode(&b, buf, MII_COREDATA_SIZE), MII_OK);
        CHECK(charinfo_equal(&a, &b, 0));
        CHECK(mii_create_id_is_null(&b.create_id));

        CHECK_EQ(mii_storedata_encode(&a, author, buf), MII_OK);
        CHECK_EQ(mii_storedata_decode(&b, buf, MII_STOREDATA_SIZE, author), MII_OK);
        CHECK(charinfo_equal(&a, &b, 1));
        CHECK_EQ(mii_storedata_decode(&b, buf, MII_STOREDATA_SIZE, NULL), MII_OK);
        CHECK_EQ(mii_storedata_decode(&b, buf, MII_STOREDATA_SIZE, (const uint8_t *)"another console!"), MII_ERR_CRC);
        buf[5] ^= 1;
        CHECK_EQ(mii_storedata_decode(&b, buf, MII_STOREDATA_SIZE, NULL), MII_ERR_CRC);
    }
}

static void test_ver3(void)
{
    for (int n = 0; n < 200; n++) {
        mii_charinfo_t a, b;
        mii_ver3_info_t ia, ib;
        uint8_t buf[MII_VER3_STOREDATA_SIZE], buf2[MII_VER3_STOREDATA_SIZE];
        make_random_mii(&a, 1);
        mii_ver3_info_init_default(&ia);
        ia.birth_month = (uint8_t)(1 + rng_next() % 12);
        ia.birth_day = (uint8_t)(1 + rng_next() % 28);
        ia.favorite = (uint8_t)(rng_next() & 1);
        ia.local_only = (uint8_t)(rng_next() & 1);
        ia.mii_id = 0x80000000u | (rng_next() & 0x0FFFFFFFu);
        memcpy(ia.creator_name, a.name, sizeof(a.name));

        CHECK_EQ(mii_ver3_encode(&a, &ia, buf), MII_OK);
        CHECK_EQ(buf[0], 3);
        CHECK_EQ(mii_crc16(buf, MII_VER3_STOREDATA_SIZE, 0), 0);
        CHECK_EQ(mii_ver3_decode(&b, &ib, buf, sizeof(buf), &a.create_id), MII_OK);
        CHECK(charinfo_equal(&a, &b, 1));
        CHECK_EQ(ib.birth_month, ia.birth_month);
        CHECK_EQ(ib.birth_day, ia.birth_day);
        CHECK_EQ(ib.local_only, ia.local_only);
        CHECK_EQ(ib.mii_id >> 31, a.type ? 0 : 1);
        CHECK_EQ(mii_ver3_encode(&b, &ib, buf2), MII_OK);
        CHECK(memcmp(buf, buf2, sizeof(buf)) == 0);
        buf[0x40] ^= 0x10;
        CHECK_EQ(mii_ver3_decode(&b, NULL, buf, sizeof(buf), NULL), MII_ERR_CRC);
    }

    /* Lossy direction: Switch-only colors and glasses fold onto Ver3. */
    mii_charinfo_t ci, back;
    uint8_t buf[MII_VER3_STOREDATA_SIZE];
    mii_charinfo_init_default(&ci, 1);
    ci.glass_type = 12;
    ci.hair_color = 22;
    ci.faceline_color = 8;
    CHECK_EQ(mii_ver3_encode(&ci, NULL, buf), MII_OK);
    CHECK_EQ(mii_ver3_decode(&back, NULL, buf, sizeof(buf), NULL), MII_OK);
    CHECK_EQ(back.glass_type, mii_glass_type_to_ver3(12));
    CHECK_EQ(back.hair_color, mii_ver3_hair_color_to_common(mii_common_to_ver3_hair_color(22)));
    CHECK_EQ(back.faceline_color, 5);

    /* Every Ver3 color maps to a Switch color and back to itself. */
    for (uint8_t v = 0; v < 8; v++) CHECK_EQ(mii_common_to_ver3_hair_color(mii_ver3_hair_color_to_common(v)), v);
    for (uint8_t v = 0; v < 6; v++) CHECK_EQ(mii_common_to_ver3_eye_color(mii_ver3_eye_color_to_common(v)), v);
    for (uint8_t v = 0; v < 5; v++) CHECK_EQ(mii_common_to_ver3_mouth_color(mii_ver3_mouth_color_to_common(v)), v);
    for (uint8_t v = 0; v < 6; v++) CHECK_EQ(mii_common_to_ver3_glass_color(mii_ver3_glass_color_to_common(v)), v);
}

static void test_nfdb(void)
{
    static mii_nfdb_t db, db2;
    static uint8_t file[MII_NFDB_SIZE];
    mii_charinfo_t m[3], got;

    mii_nfdb_init(&db);
    for (int i = 0; i < 3; i++) {
        make_random_mii(&m[i], 0);
        CHECK_EQ(mii_nfdb_put(&db, &m[i], NULL), i);
    }
    CHECK_EQ(db.count, 3);
    /* replacing keeps the slot */
    m[1].hair_type = 7;
    CHECK_EQ(mii_nfdb_put(&db, &m[1], NULL), 1);
    CHECK_EQ(db.count, 3);
    CHECK_EQ(mii_nfdb_get(&db, 1, &got), MII_OK);
    CHECK_EQ(got.hair_type, 7);
    CHECK_EQ(mii_nfdb_find(&db, &m[2].create_id), 2);

    CHECK_EQ(mii_nfdb_move(&db, 2, 0), MII_OK);
    CHECK_EQ(mii_nfdb_find(&db, &m[2].create_id), 0);
    CHECK_EQ(mii_nfdb_find(&db, &m[0].create_id), 1);

    CHECK_EQ(mii_nfdb_encode(&db, file), MII_OK);
    CHECK(memcmp(file, "NFDB", 4) == 0);
    CHECK_EQ(mii_nfdb_decode(&db2, file, sizeof(file)), MII_OK);
    CHECK_EQ(db2.count, 3);
    CHECK_EQ(mii_nfdb_get(&db2, 0, &got), MII_OK);
    CHECK(charinfo_equal(&got, &m[2], 1));

    CHECK_EQ(mii_nfdb_remove(&db2, 0), MII_OK);
    CHECK_EQ(db2.count, 2);
    CHECK_EQ(mii_nfdb_find(&db2, &m[2].create_id), MII_ERR_NOT_FOUND);

    file[100] ^= 1;
    CHECK_EQ(mii_nfdb_decode(&db2, file, sizeof(file)), MII_ERR_CRC);

    /* a Mii without a valid create id is refused */
    mii_charinfo_init_default(&got, 0);
    CHECK_EQ(mii_nfdb_put(&db, &got, NULL), MII_ERR_INVALID);

    mii_nfdb_init(&db);
    for (int i = 0; i < MII_NFDB_MAX_MIIS; i++) {
        make_random_mii(&got, 0);
        CHECK_EQ(mii_nfdb_put(&db, &got, NULL), i);
    }
    make_random_mii(&got, 0);
    CHECK_EQ(mii_nfdb_put(&db, &got, NULL), MII_ERR_FULL);
}

static void test_editor_data(void)
{
    static const struct { mii_grid_kind_t kind; mii_field_t field; } grids[] = {
        { MII_GRID_COMMON_COLOR, MII_FIELD_HAIR_COLOR },
        { MII_GRID_FACELINE_COLOR, MII_FIELD_FACELINE_COLOR },
        { MII_GRID_HAIR, MII_FIELD_HAIR_TYPE },
        { MII_GRID_EYE, MII_FIELD_EYE_TYPE },
        { MII_GRID_EYEBROW, MII_FIELD_EYEBROW_TYPE },
        { MII_GRID_NOSE, MII_FIELD_NOSE_TYPE },
        { MII_GRID_MOUTH, MII_FIELD_MOUTH_TYPE },
        { MII_GRID_GLASS, MII_FIELD_GLASS_TYPE },
    };
    /* Every grid is a permutation of its field's full range. */
    for (size_t g = 0; g < sizeof(grids) / sizeof(grids[0]); g++) {
        const mii_grid_t *grid = mii_editor_grid(grids[g].kind);
        int n = mii_field_max(grids[g].field) + 1;
        uint8_t seen[256] = { 0 };
        CHECK(grid != NULL);
        CHECK_EQ(grid->count, n);
        CHECK_EQ(grid->count % grid->columns, 0);
        for (int i = 0; i < grid->count; i++) {
            CHECK(grid->items[i] < n);
            seen[grid->items[i]]++;
        }
        for (int v = 0; v < n; v++)
            CHECK_EQ(seen[v], 1);
        CHECK_EQ(mii_grid_index_of(grid, grid->items[7 % grid->count]), 7 % grid->count);
    }
    /* Palette spot checks: Ver3 hair colors 1-7 equal common colors 1-7,
     * and common color 8 (Ver3 hair 0 / default eyes) is black. */
    CHECK_EQ(mii_common_color_rgb(1), 0x402010);
    CHECK_EQ(mii_common_color_rgb(7), 0xD0A04A);
    CHECK_EQ(mii_common_color_rgb(8), 0x000000);
    CHECK_EQ(mii_common_color_rgb(99), 0xFFFFFF);
    CHECK_EQ(mii_faceline_color_rgb(0), 0xFFD3AD);
    CHECK_EQ(mii_faceline_color_rgb(5), 0x632C18);
}

/* Synthetic Miis vs the bytes MiiJS produced for them. */
static void test_vectors(const char *dir)
{
    int found = 0;
    g_rng = 0x76656374u;
    for (int n = 0; n < VECTOR_COUNT; n++) {
        char path[1024];
        uint8_t mine[0x60], ref[0x100];
        long len;
        mii_charinfo_t ci;
        make_random_mii(&ci, 0);

        snprintf(path, sizeof(path), "%s/random_%02d.nfcd", dir, n);
        if ((len = read_file(path, ref, sizeof(ref))) < 0)
            continue;
        found++;
        CHECK_EQ(len, MII_COREDATA_SIZE);
        mii_coredata_encode(&ci, mine);
        CHECK(memcmp(mine, ref, MII_COREDATA_SIZE) == 0);

        snprintf(path, sizeof(path), "%s/random_%02d.nfsd", dir, n);
        len = read_file(path, ref, sizeof(ref));
        CHECK_EQ(len, MII_STOREDATA_SIZE);
        mii_storedata_encode(&ci, NULL, mine);
        CHECK(memcmp(mine, ref, 0x42) == 0); /* MiiJS cannot compute the device CRC */

        snprintf(path, sizeof(path), "%s/random_%02d.ffsd", dir, n);
        len = read_file(path, ref, sizeof(ref));
        CHECK_EQ(len, MII_VER3_STOREDATA_SIZE);
        mii_ver3_encode(&ci, NULL, mine);
        CHECK(memcmp(mine + 0x18, ref + 0x18, 0x48 - 0x18) == 0); /* Mii body; header defaults differ */
        CHECK_EQ(mii_crc16(ref, MII_VER3_STOREDATA_SIZE, 0), 0);
    }
    if (!found)
        fprintf(stderr, "note: no test vectors found in %s\n", dir);
    else
        CHECK_EQ(found, VECTOR_COUNT);
}

/* ---- modes ------------------------------------------------------------- */

static int gen_vectors(const char *dir)
{
    g_rng = 0x76656374u;
    for (int n = 0; n < VECTOR_COUNT; n++) {
        char path[1024];
        uint8_t buf[MII_CHARINFO_SIZE];
        mii_charinfo_t ci;
        make_random_mii(&ci, 0);
        mii_charinfo_encode(&ci, buf);
        snprintf(path, sizeof(path), "%s/random_%02d.charinfo", dir, n);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(buf, 1, sizeof(buf), f) != sizeof(buf)) {
            perror(path);
            return 1;
        }
        fclose(f);
    }
    return 0;
}

static int ver3_roundtrip(int argc, char **argv)
{
    int bad = 0;
    for (int i = 0; i < argc; i++) {
        uint8_t in[0x100], out[MII_VER3_STOREDATA_SIZE];
        mii_charinfo_t ci;
        mii_ver3_info_t info;
        long n = read_file(argv[i], in, sizeof(in));
        mii_result_t r = n < 0 ? MII_ERR_ARGUMENT : mii_ver3_decode(&ci, &info, in, (size_t)n, NULL);
        if (r != MII_OK) {
            printf("ERR  %s: %s\n", argv[i], mii_result_string(r));
            bad++;
            continue;
        }
        mii_ver3_encode(&ci, &info, out);
        /* Ignore units after each name terminator (dropped on decode). */
        int diffs = 0;
        for (int o = 0; o < 0x5C; o++) {
            int in_name = (o >= 0x1A && o < 0x2E) || (o >= 0x48 && o < 0x5C);
            if (in_name) {
                int base = o < 0x48 ? 0x1A : 0x48, unit = (o - base) / 2, ended = 0;
                for (int u = 0; u <= unit; u++)
                    if (!(in[base + 2 * u] | in[base + 2 * u + 1])) ended = 1;
                if (ended)
                    continue;
            }
            if (in[o] != out[o]) {
                if (!diffs)
                    printf("DIFF %s:", argv[i]);
                printf(" 0x%02x(%02x->%02x)", o, in[o], out[o]);
                diffs++;
            }
        }
        if (diffs) {
            printf("\n");
            bad++;
        }
    }
    printf("ver3 roundtrip: %d/%d exact\n", argc - bad, argc);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--gen-vectors"))
        return gen_vectors(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "--ver3-roundtrip"))
        return ver3_roundtrip(argc - 2, argv + 2);

    test_crc();
    test_default();
    test_fields();
    test_names();
    test_create_id();
    test_switch_roundtrips();
    test_ver3();
    test_nfdb();
    test_editor_data();
#ifdef VECTOR_DIR
    test_vectors(argc > 1 ? argv[1] : VECTOR_DIR);
#endif
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
