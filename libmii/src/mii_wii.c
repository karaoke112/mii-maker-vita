/* The Wii's Mii format (RFLCharData, 0x4A bytes) and its Mii database
 * (RFL_DB.dat), as the Wii's face library (RVLFaceLib) reads them.
 *
 * Layout: big-endian; each 16-bit word holds bit-fields from the most
 * significant bit down, so the whole record reads as one bit stream from
 * the first byte's top bit. Field order and widths follow RFLiCharData.
 *
 * The Wii has fewer parts than later consoles. Part numbers it has are the
 * same as the 3DS's (ver3); newer ones are replaced by the default part,
 * and the colors go through the 3DS's color tables. */
#include "mii_internal.h"

#include <string.h>

/* ---- bit stream ---------------------------------------------------------- */

static void put(uint8_t *p, unsigned bit, unsigned width, unsigned value)
{
    for (unsigned i = 0; i < width; i++) {
        const unsigned b = bit + i, v = (value >> (width - 1 - i)) & 1u;
        uint8_t *byte = p + b / 8;
        const uint8_t mask = (uint8_t)(0x80u >> (b % 8));
        *byte = v ? (uint8_t)(*byte | mask) : (uint8_t)(*byte & ~mask);
    }
}

static unsigned get(const uint8_t *p, unsigned bit, unsigned width)
{
    unsigned v = 0;
    for (unsigned i = 0; i < width; i++) {
        const unsigned b = bit + i;
        v = (v << 1) | ((p[b / 8] >> (7 - b % 8)) & 1u);
    }
    return v;
}

#define AT(byte) ((byte) * 8u)

/* ---- facial features ------------------------------------------------------ */

/* The Wii's one "face texture" is a makeup or a wrinkle pattern of the
 * later consoles: { makeup, wrinkle } for each of its 12 values. */
static const uint8_t k_face_tex[12][2] = {
    { 0, 0 }, { 1, 0 }, { 6, 0 }, { 9, 0 }, { 0, 5 }, { 0, 2 },
    { 0, 3 }, { 0, 7 }, { 0, 8 }, { 10, 0 }, { 0, 9 }, { 0, 11 },
};

static unsigned face_tex_for(unsigned make, unsigned wrinkle)
{
    /* the makeup shows more, so it wins when both are set */
    for (unsigned t = 1; t < 12; t++)
        if (make && k_face_tex[t][0] == make)
            return t;
    for (unsigned t = 1; t < 12; t++)
        if (wrinkle && k_face_tex[t][1] == wrinkle)
            return t;
    return 0;
}

/* ---- conversions --------------------------------------------------------- */

static unsigned or_default(unsigned value, unsigned limit, unsigned fallback)
{
    return value < limit ? value : fallback;
}

int mii_wii_compatible(const mii_charinfo_t *ci)
{
    if (!ci)
        return 0;
    const uint8_t make = ci->faceline_make, wrinkle = ci->faceline_wrinkle;
    int tex_ok = (make == 0 && wrinkle == 0);
    for (unsigned t = 1; t < 12 && !tex_ok; t++)
        tex_ok = k_face_tex[t][0] == make && k_face_tex[t][1] == wrinkle;
    return tex_ok && ci->faceline_type < 8 && ci->hair_type < 72 && ci->eye_type < 48 &&
           ci->nose_type < 12 && ci->mouth_type < 24 && ci->mustache_type < 4 && ci->beard_type < 4 &&
           ci->glass_type < 9 && mii_faceline_color_to_ver3(ci->faceline_color) == ci->faceline_color &&
           mii_common_to_ver3_mouth_color(ci->mouth_color) < 3;
}

uint32_t mii_wii_id_for(const mii_charinfo_t *ci)
{
    /* stable for a Mii (its create ID), so games that remember a Mii by ID
     * (save-file icons) find it again; bit 31 set: a normal Mii, bit 29
     * clear: not a temporary one */
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) {
        h ^= ci->create_id.bytes[i];
        h *= 16777619u;
    }
    h &= 0x1FFFFFFFu;
    if (h == 0)
        h = 1;
    return 0x80000000u | h;
}

void mii_wii_info_init_default(mii_wii_info_t *info)
{
    memset(info, 0, sizeof(*info));
}

mii_result_t mii_wii_encode(const mii_charinfo_t *ci, const mii_wii_info_t *info, uint8_t out[MII_WII_DATA_SIZE])
{
    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    mii_wii_info_t def;
    if (!info) {
        mii_wii_info_init_default(&def);
        info = &def;
    }
    mii_charinfo_t d; /* default parts, for ones the Wii lacks */
    mii_charinfo_init_default(&d, ci->gender);
    memset(out, 0, MII_WII_DATA_SIZE);

    put(out, 1, 1, ci->gender & 1);
    put(out, 2, 4, info->birth_month <= 12 ? info->birth_month : 0);
    put(out, 6, 5, info->birth_day <= 31 ? info->birth_day : 0);
    put(out, 11, 4, or_default(ci->favorite_color, 12, 0));
    put(out, 15, 1, info->favorite & 1);
    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16be(out + 0x02 + 2 * i, ci->name[i]);
    out[0x16] = ci->height > 127 ? 127 : ci->height;
    out[0x17] = ci->build > 127 ? 127 : ci->build;
    mii_wr32be(out + 0x18, info->mii_id ? info->mii_id : mii_wii_id_for(ci));
    memcpy(out + 0x1C, info->system_id, 4);

    put(out, AT(0x20), 3, or_default(ci->faceline_type, 8, d.faceline_type));
    put(out, AT(0x20) + 3, 3, mii_faceline_color_to_ver3(ci->faceline_color));
    put(out, AT(0x20) + 6, 4, face_tex_for(ci->faceline_make, ci->faceline_wrinkle));
    put(out, AT(0x20) + 13, 1, info->local_only & 1);

    put(out, AT(0x22), 7, or_default(ci->hair_type, 72, d.hair_type));
    put(out, AT(0x22) + 7, 3, mii_common_to_ver3_hair_color(ci->hair_color));
    put(out, AT(0x22) + 10, 1, ci->hair_flip & 1);

    put(out, AT(0x24), 5, or_default(ci->eyebrow_type, 24, d.eyebrow_type));
    put(out, AT(0x24) + 5, 5, ci->eyebrow_rotate);
    put(out, AT(0x26), 3, mii_common_to_ver3_hair_color(ci->eyebrow_color));
    put(out, AT(0x26) + 3, 4, ci->eyebrow_scale);
    put(out, AT(0x26) + 7, 5, ci->eyebrow_y);
    put(out, AT(0x26) + 12, 4, ci->eyebrow_x);

    put(out, AT(0x28), 6, or_default(ci->eye_type, 48, d.eye_type));
    put(out, AT(0x28) + 6, 5, ci->eye_rotate);
    put(out, AT(0x28) + 11, 5, ci->eye_y);
    put(out, AT(0x2A), 3, mii_common_to_ver3_eye_color(ci->eye_color));
    put(out, AT(0x2A) + 3, 4, ci->eye_scale);
    put(out, AT(0x2A) + 7, 4, ci->eye_x);

    put(out, AT(0x2C), 4, or_default(ci->nose_type, 12, d.nose_type));
    put(out, AT(0x2C) + 4, 4, ci->nose_scale);
    put(out, AT(0x2C) + 8, 5, ci->nose_y);

    /* the Wii has three lip colors (orange, red, pink); the 3DS's peach
     * and black become orange and red */
    static const uint8_t k_lips[5] = { 0, 1, 2, 0, 1 };
    put(out, AT(0x2E), 5, or_default(ci->mouth_type, 24, d.mouth_type));
    put(out, AT(0x2E) + 5, 2, k_lips[mii_common_to_ver3_mouth_color(ci->mouth_color) % 5]);
    put(out, AT(0x2E) + 7, 4, ci->mouth_scale);
    put(out, AT(0x2E) + 11, 5, ci->mouth_y);

    put(out, AT(0x30), 4, mii_glass_type_to_ver3(ci->glass_type));
    put(out, AT(0x30) + 4, 3, mii_common_to_ver3_glass_color(ci->glass_color));
    put(out, AT(0x30) + 7, 4, ci->glass_scale);
    put(out, AT(0x30) + 11, 5, ci->glass_y);

    put(out, AT(0x32), 2, or_default(ci->mustache_type, 4, 0));
    put(out, AT(0x32) + 2, 2, or_default(ci->beard_type, 4, 0));
    put(out, AT(0x32) + 4, 3, mii_common_to_ver3_hair_color(ci->beard_color));
    put(out, AT(0x32) + 7, 4, ci->mustache_scale);
    put(out, AT(0x32) + 11, 5, ci->mustache_y);

    put(out, AT(0x34), 1, ci->mole_type & 1);
    put(out, AT(0x34) + 1, 4, ci->mole_scale);
    put(out, AT(0x34) + 5, 5, ci->mole_y);
    put(out, AT(0x34) + 10, 5, ci->mole_x);

    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16be(out + 0x36 + 2 * i, info->creator_name[i]);
    return MII_OK;
}

mii_result_t mii_wii_decode(mii_charinfo_t *ci, mii_wii_info_t *info, const uint8_t *data, size_t size)
{
    if (!ci || !data)
        return MII_ERR_ARGUMENT;
    if (size < MII_WII_DATA_SIZE)
        return MII_ERR_SIZE;
    const unsigned gender = get(data, 1, 1);
    mii_charinfo_init_default(ci, (int)gender);
    if (info) {
        mii_wii_info_init_default(info);
        info->birth_month = (uint8_t)get(data, 2, 4);
        info->birth_day = (uint8_t)get(data, 6, 5);
        info->favorite = (uint8_t)get(data, 15, 1);
        info->mii_id = mii_rd32be(data + 0x18);
        memcpy(info->system_id, data + 0x1C, 4);
        info->local_only = (uint8_t)get(data, AT(0x20) + 13, 1);
        for (int i = 0; i < MII_NAME_LENGTH; i++)
            info->creator_name[i] = mii_rd16be(data + 0x36 + 2 * i);
    }
    ci->favorite_color = (uint8_t)or_default(get(data, 11, 4), 12, 0);
    memset(ci->name, 0, sizeof(ci->name));
    for (int i = 0; i < MII_NAME_LENGTH; i++)
        ci->name[i] = mii_rd16be(data + 0x02 + 2 * i);
    ci->height = data[0x16] > 127 ? 127 : data[0x16];
    ci->build = data[0x17] > 127 ? 127 : data[0x17];
    /* a create ID of the later consoles' kind, made from the Wii's Mii ID
     * and system ID (so the same Wii Mii always gets the same one) */
    uint8_t seed[16];
    for (int i = 0; i < 16; i++)
        seed[i] = (uint8_t)(data[0x18 + i % 8] ^ (i < 8 ? 0x00 : 0xA5));
    mii_create_id_from_random(&ci->create_id, seed);

    ci->faceline_type = (uint8_t)get(data, AT(0x20), 3);
    ci->faceline_color = (uint8_t)or_default(get(data, AT(0x20) + 3, 3), 6, 0);
    const unsigned tex = or_default(get(data, AT(0x20) + 6, 4), 12, 0);
    ci->faceline_make = k_face_tex[tex][0];
    ci->faceline_wrinkle = k_face_tex[tex][1];

    ci->hair_type = (uint8_t)get(data, AT(0x22), 7);
    ci->hair_color = mii_ver3_hair_color_to_common((uint8_t)get(data, AT(0x22) + 7, 3));
    ci->hair_flip = (uint8_t)get(data, AT(0x22) + 10, 1);

    ci->eyebrow_type = (uint8_t)get(data, AT(0x24), 5);
    ci->eyebrow_rotate = (uint8_t)get(data, AT(0x24) + 5, 5);
    ci->eyebrow_color = mii_ver3_hair_color_to_common((uint8_t)get(data, AT(0x26), 3));
    ci->eyebrow_scale = (uint8_t)get(data, AT(0x26) + 3, 4);
    ci->eyebrow_y = (uint8_t)get(data, AT(0x26) + 7, 5);
    ci->eyebrow_x = (uint8_t)get(data, AT(0x26) + 12, 4);
    ci->eyebrow_aspect = 3; /* the Wii's proportions */

    ci->eye_type = (uint8_t)get(data, AT(0x28), 6);
    ci->eye_rotate = (uint8_t)get(data, AT(0x28) + 6, 5);
    ci->eye_y = (uint8_t)get(data, AT(0x28) + 11, 5);
    ci->eye_color = mii_ver3_eye_color_to_common((uint8_t)get(data, AT(0x2A), 3));
    ci->eye_scale = (uint8_t)get(data, AT(0x2A) + 3, 4);
    ci->eye_x = (uint8_t)get(data, AT(0x2A) + 7, 4);
    ci->eye_aspect = 3;

    ci->nose_type = (uint8_t)get(data, AT(0x2C), 4);
    ci->nose_scale = (uint8_t)get(data, AT(0x2C) + 4, 4);
    ci->nose_y = (uint8_t)get(data, AT(0x2C) + 8, 5);

    ci->mouth_type = (uint8_t)get(data, AT(0x2E), 5);
    ci->mouth_color = mii_ver3_mouth_color_to_common((uint8_t)get(data, AT(0x2E) + 5, 2));
    ci->mouth_scale = (uint8_t)get(data, AT(0x2E) + 7, 4);
    ci->mouth_y = (uint8_t)get(data, AT(0x2E) + 11, 5);
    ci->mouth_aspect = 3;

    ci->glass_type = (uint8_t)or_default(get(data, AT(0x30), 4), 9, 0);
    ci->glass_color = mii_ver3_glass_color_to_common((uint8_t)get(data, AT(0x30) + 4, 3));
    ci->glass_scale = (uint8_t)get(data, AT(0x30) + 7, 4);
    ci->glass_y = (uint8_t)get(data, AT(0x30) + 11, 5);

    ci->mustache_type = (uint8_t)get(data, AT(0x32), 2);
    ci->beard_type = (uint8_t)get(data, AT(0x32) + 2, 2);
    ci->beard_color = mii_ver3_hair_color_to_common((uint8_t)get(data, AT(0x32) + 4, 3));
    ci->mustache_scale = (uint8_t)get(data, AT(0x32) + 7, 4);
    ci->mustache_y = (uint8_t)get(data, AT(0x32) + 11, 5);

    ci->mole_type = (uint8_t)get(data, AT(0x34), 1);
    ci->mole_scale = (uint8_t)get(data, AT(0x34) + 1, 4);
    ci->mole_y = (uint8_t)get(data, AT(0x34) + 5, 5);
    ci->mole_x = (uint8_t)get(data, AT(0x34) + 10, 5);
    return MII_OK;
}

/* ---- the database (RFL_DB.dat) ---------------------------------------- */

/* RFLiDatabase: 'RNOD', 100 Miis, flags, then the hidden database's header
 * ('RNHD', 10000 empty table entries) ending in a CRC; then room for the
 * 10000 hidden Miis, as RFL's format writes it. */
#define DB_MIIS          0x04
#define DB_FLAGS         0x1CEC
#define DB_HIDDEN        0x1D00
#define DB_HIDDEN_TABLE  (DB_HIDDEN + 0x08)
#define DB_HIDDEN_ENTRY  0x0C
#define DB_HIDDEN_COUNT  10000
#define DB_HEADER_SIZE   0x3B1E0 /* sizeof(RFLiDatabase), the CRC's span */

uint16_t mii_wii_crc(const uint8_t *p, size_t len)
{
    /* RFLiCalculateCRC: CRC-16 (0x1021), the data shifted in at the bottom */
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t data = p[i];
        for (int j = 0; j < 8; j++, data = (uint8_t)(data << 1)) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
            if (data & 0x80)
                crc ^= 1;
        }
    }
    return crc;
}

mii_result_t mii_wii_db_write(const uint8_t (*miis)[MII_WII_DATA_SIZE], int count, uint8_t *out, size_t out_size)
{
    if (!out || count < 0 || (count > 0 && !miis))
        return MII_ERR_ARGUMENT;
    if (out_size < MII_WII_DB_SIZE)
        return MII_ERR_SIZE;
    if (count > MII_WII_DB_MAX)
        count = MII_WII_DB_MAX;
    memset(out, 0, MII_WII_DB_SIZE);
    memcpy(out, "RNOD", 4);
    for (int i = 0; i < count; i++)
        memcpy(out + DB_MIIS + i * MII_WII_DATA_SIZE, miis[i], MII_WII_DATA_SIZE);
    out[DB_FLAGS] = 0x80; /* isolation (as a freshly formatted database) */
    memcpy(out + DB_HIDDEN, "RNHD", 4);
    mii_wr16be(out + DB_HIDDEN + 4, 0xFFFF); /* head: none */
    mii_wr16be(out + DB_HIDDEN + 6, 0xFFFF); /* tail: none */
    for (int i = 0; i < DB_HIDDEN_COUNT; i++) {
        uint8_t *e = out + DB_HIDDEN_TABLE + i * DB_HIDDEN_ENTRY;
        /* { create ID, sex:1 next:15 = -1, padding:1 prev:15 = -1 } */
        mii_wr16be(e + 8, 0x7FFF);
        mii_wr16be(e + 10, 0x7FFF);
    }
    /* the CRC over the whole header with its own field zero, stored there */
    mii_wr16be(out + DB_HEADER_SIZE - 2, mii_wii_crc(out, DB_HEADER_SIZE));
    return MII_OK;
}

int mii_wii_db_valid(const uint8_t *db, size_t size)
{
    return db && size >= DB_HEADER_SIZE && memcmp(db, "RNOD", 4) == 0 && mii_wii_crc(db, DB_HEADER_SIZE) == 0;
}

int mii_wii_db_get(const uint8_t *db, size_t size, int index, uint8_t out[MII_WII_DATA_SIZE])
{
    if (!mii_wii_db_valid(db, size) || index < 0 || index >= MII_WII_DB_MAX)
        return 0;
    const uint8_t *e = db + DB_MIIS + index * MII_WII_DATA_SIZE;
    if (mii_rd16be(e + 2) == 0) /* no name: an empty slot */
        return 0;
    memcpy(out, e, MII_WII_DATA_SIZE);
    return 1;
}
