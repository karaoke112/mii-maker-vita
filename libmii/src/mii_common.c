#include "mii_internal.h"

const char *mii_result_string(mii_result_t r)
{
    switch (r) {
    case MII_OK:            return "ok";
    case MII_ERR_ARGUMENT:  return "invalid argument";
    case MII_ERR_SIZE:      return "wrong data size";
    case MII_ERR_CRC:       return "checksum mismatch";
    case MII_ERR_INVALID:   return "field out of range";
    case MII_ERR_MAGIC:     return "bad magic or version";
    case MII_ERR_FULL:      return "database full";
    case MII_ERR_NOT_FOUND: return "not found";
    case MII_ERR_NO_KEY:    return "key not provided";
    }
    return "unknown error";
}

uint16_t mii_crc16(const uint8_t *data, size_t size, uint16_t seed)
{
    uint32_t crc = seed;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint32_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if (crc & 0x10000u)
                crc = (crc ^ 0x1021u) & 0xFFFFu;
        }
    }
    return (uint16_t)crc;
}

/* ---- create id --------------------------------------------------------- */

void mii_create_id_from_random(mii_create_id_t *id, const uint8_t random16[16])
{
    memcpy(id->bytes, random16, 16);
    id->bytes[6] = (uint8_t)((id->bytes[6] & 0x0F) | 0x40); /* version 4 */
    id->bytes[8] = (uint8_t)((id->bytes[8] & 0x3F) | 0x80); /* variant: nn::mii checks this */
}

int mii_create_id_is_null(const mii_create_id_t *id)
{
    for (int i = 0; i < 16; i++)
        if (id->bytes[i])
            return 0;
    return 1;
}

int mii_create_id_is_valid(const mii_create_id_t *id)
{
    return !mii_create_id_is_null(id) && (id->bytes[8] & 0xC0) == 0x80;
}

int mii_create_id_equal(const mii_create_id_t *a, const mii_create_id_t *b)
{
    return memcmp(a->bytes, b->bytes, 16) == 0;
}

/* ---- field table ------------------------------------------------------- */

typedef struct field_desc {
    const char *name;
    uint8_t min, max;
} field_desc;

static const field_desc k_fields[MII_FIELD_COUNT] = {
    [MII_FIELD_FONT_REGION]      = { "font_region",      0, 3 },
    [MII_FIELD_FAVORITE_COLOR]   = { "favorite_color",   0, 11 },
    [MII_FIELD_GENDER]           = { "gender",           0, 1 },
    [MII_FIELD_HEIGHT]           = { "height",           0, 127 },
    [MII_FIELD_BUILD]            = { "build",            0, 127 },
    [MII_FIELD_TYPE]             = { "type",             0, 1 },
    [MII_FIELD_REGION_MOVE]      = { "region_move",      0, 3 },
    [MII_FIELD_FACELINE_TYPE]    = { "faceline_type",    0, 11 },
    [MII_FIELD_FACELINE_COLOR]   = { "faceline_color",   0, 9 },
    [MII_FIELD_FACELINE_WRINKLE] = { "faceline_wrinkle", 0, 11 },
    [MII_FIELD_FACELINE_MAKE]    = { "faceline_make",    0, 11 },
    [MII_FIELD_HAIR_TYPE]        = { "hair_type",        0, 131 },
    [MII_FIELD_HAIR_COLOR]       = { "hair_color",       0, 99 },
    [MII_FIELD_HAIR_FLIP]        = { "hair_flip",        0, 1 },
    [MII_FIELD_EYE_TYPE]         = { "eye_type",         0, 59 },
    [MII_FIELD_EYE_COLOR]        = { "eye_color",        0, 99 },
    [MII_FIELD_EYE_SCALE]        = { "eye_scale",        0, 7 },
    [MII_FIELD_EYE_ASPECT]       = { "eye_aspect",       0, 6 },
    [MII_FIELD_EYE_ROTATE]       = { "eye_rotate",       0, 7 },
    [MII_FIELD_EYE_X]            = { "eye_x",            0, 12 },
    [MII_FIELD_EYE_Y]            = { "eye_y",            0, 18 },
    [MII_FIELD_EYEBROW_TYPE]     = { "eyebrow_type",     0, 23 },
    [MII_FIELD_EYEBROW_COLOR]    = { "eyebrow_color",    0, 99 },
    [MII_FIELD_EYEBROW_SCALE]    = { "eyebrow_scale",    0, 8 },
    [MII_FIELD_EYEBROW_ASPECT]   = { "eyebrow_aspect",   0, 6 },
    [MII_FIELD_EYEBROW_ROTATE]   = { "eyebrow_rotate",   0, 11 },
    [MII_FIELD_EYEBROW_X]        = { "eyebrow_x",        0, 12 },
    [MII_FIELD_EYEBROW_Y]        = { "eyebrow_y",        3, 18 },
    [MII_FIELD_NOSE_TYPE]        = { "nose_type",        0, 17 },
    [MII_FIELD_NOSE_SCALE]       = { "nose_scale",       0, 8 },
    [MII_FIELD_NOSE_Y]           = { "nose_y",           0, 18 },
    [MII_FIELD_MOUTH_TYPE]       = { "mouth_type",       0, 35 },
    [MII_FIELD_MOUTH_COLOR]      = { "mouth_color",      0, 99 },
    [MII_FIELD_MOUTH_SCALE]      = { "mouth_scale",      0, 8 },
    [MII_FIELD_MOUTH_ASPECT]     = { "mouth_aspect",     0, 6 },
    [MII_FIELD_MOUTH_Y]          = { "mouth_y",          0, 18 },
    [MII_FIELD_BEARD_COLOR]      = { "beard_color",      0, 99 },
    [MII_FIELD_BEARD_TYPE]       = { "beard_type",       0, 5 },
    [MII_FIELD_MUSTACHE_TYPE]    = { "mustache_type",    0, 5 },
    [MII_FIELD_MUSTACHE_SCALE]   = { "mustache_scale",   0, 8 },
    [MII_FIELD_MUSTACHE_Y]       = { "mustache_y",       0, 16 },
    [MII_FIELD_GLASS_TYPE]       = { "glass_type",       0, 19 },
    [MII_FIELD_GLASS_COLOR]      = { "glass_color",      0, 99 },
    [MII_FIELD_GLASS_SCALE]      = { "glass_scale",      0, 7 },
    [MII_FIELD_GLASS_Y]          = { "glass_y",          0, 20 },
    [MII_FIELD_MOLE_TYPE]        = { "mole_type",        0, 1 },
    [MII_FIELD_MOLE_SCALE]       = { "mole_scale",       0, 8 },
    [MII_FIELD_MOLE_X]           = { "mole_x",           0, 16 },
    [MII_FIELD_MOLE_Y]           = { "mole_y",           0, 30 },
};

/* The 49 field bytes are laid out contiguously from font_region to mole_y,
 * in mii_field_t order. */
#define FIELD_BASE offsetof(mii_charinfo_t, font_region)
typedef char mii_assert_field_layout[
    (offsetof(mii_charinfo_t, mole_y) - FIELD_BASE == MII_FIELD_MOLE_Y) ? 1 : -1];

static const uint8_t *field_ptr_c(const mii_charinfo_t *ci, mii_field_t f)
{
    return (const uint8_t *)ci + FIELD_BASE + (size_t)f;
}

const char *mii_field_name(mii_field_t field)
{
    return (unsigned)field < MII_FIELD_COUNT ? k_fields[field].name : "?";
}

uint8_t mii_field_min(mii_field_t field)
{
    return (unsigned)field < MII_FIELD_COUNT ? k_fields[field].min : 0;
}

uint8_t mii_field_max(mii_field_t field)
{
    return (unsigned)field < MII_FIELD_COUNT ? k_fields[field].max : 0;
}

uint8_t mii_field_get(const mii_charinfo_t *ci, mii_field_t field)
{
    if (!ci || (unsigned)field >= MII_FIELD_COUNT)
        return 0;
    return *field_ptr_c(ci, field);
}

uint8_t mii_field_set(mii_charinfo_t *ci, mii_field_t field, int value)
{
    if (!ci || (unsigned)field >= MII_FIELD_COUNT)
        return 0;
    if (value < k_fields[field].min) value = k_fields[field].min;
    if (value > k_fields[field].max) value = k_fields[field].max;
    *((uint8_t *)ci + FIELD_BASE + (size_t)field) = (uint8_t)value;
    return (uint8_t)value;
}

int mii_fields_in_range(const mii_charinfo_t *ci)
{
    for (int f = 0; f < MII_FIELD_COUNT; f++) {
        uint8_t v = *field_ptr_c(ci, (mii_field_t)f);
        if (v < k_fields[f].min || v > k_fields[f].max)
            return 0;
    }
    return 1;
}

/* ---- names ------------------------------------------------------------- */

static int name_is_valid(const uint16_t name[MII_NAME_LENGTH + 1])
{
    int len = -1;
    for (int i = 0; i <= MII_NAME_LENGTH; i++) {
        if (name[i] == 0) { len = i; break; }
    }
    if (len <= 0)
        return 0; /* unterminated or empty */
    for (int i = 0; i < len; i++) {
        uint16_t c = name[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= len || name[i + 1] < 0xDC00 || name[i + 1] > 0xDFFF)
                return 0;
            i++;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            return 0;
        }
    }
    return 1;
}

size_t mii_name_set_utf8(mii_charinfo_t *ci, const char *utf8)
{
    const unsigned char *s = (const unsigned char *)utf8;
    size_t n = 0;
    memset(ci->name, 0, sizeof(ci->name));
    while (s && *s && n < MII_NAME_LENGTH) {
        uint32_t cp;
        int extra;
        if (*s < 0x80)              { cp = *s; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
        else { s++; continue; }
        s++;
        for (int i = 0; i < extra; i++) {
            if ((*s & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (*s++ & 0x3F);
        }
        if (cp >= 0x10000) {
            if (n + 2 > MII_NAME_LENGTH)
                break;
            cp -= 0x10000;
            ci->name[n++] = (uint16_t)(0xD800 + (cp >> 10));
            ci->name[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            ci->name[n++] = (uint16_t)cp;
        }
    }
    return n;
}

size_t mii_name_get_utf8(const mii_charinfo_t *ci, char *out, size_t out_size)
{
    size_t o = 0;
    if (!out || out_size == 0)
        return 0;
    for (int i = 0; i < MII_NAME_LENGTH && ci->name[i]; i++) {
        uint32_t cp = ci->name[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < MII_NAME_LENGTH &&
            ci->name[i + 1] >= 0xDC00 && ci->name[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (ci->name[i + 1] - 0xDC00);
            i++;
        }
        unsigned char buf[4];
        size_t len;
        if (cp < 0x80)         { buf[0] = (unsigned char)cp; len = 1; }
        else if (cp < 0x800)   { buf[0] = (unsigned char)(0xC0 | (cp >> 6)); buf[1] = (unsigned char)(0x80 | (cp & 0x3F)); len = 2; }
        else if (cp < 0x10000) { buf[0] = (unsigned char)(0xE0 | (cp >> 12)); buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (unsigned char)(0x80 | (cp & 0x3F)); len = 3; }
        else                   { buf[0] = (unsigned char)(0xF0 | (cp >> 18)); buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F)); buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); buf[3] = (unsigned char)(0x80 | (cp & 0x3F)); len = 4; }
        if (o + len + 1 > out_size)
            break;
        memcpy(out + o, buf, len);
        o += len;
    }
    out[o] = '\0';
    return o;
}

int mii_verify(const mii_charinfo_t *ci, int check_create_id)
{
    for (int f = 0; f < MII_FIELD_COUNT; f++) {
        uint8_t v = *field_ptr_c(ci, (mii_field_t)f);
        if (v < k_fields[f].min || v > k_fields[f].max)
            return f;
    }
    if (!name_is_valid(ci->name))
        return MII_FIELD_COUNT + 1;
    if (check_create_id && !mii_create_id_is_valid(&ci->create_id))
        return MII_FIELD_COUNT + 2;
    return MII_FIELD_COUNT;
}

/* ---- defaults ---------------------------------------------------------- */

/* nn::mii's two base templates (DefaultMii 0 and 1), with their Ver3 colors
 * already mapped to the Switch palette. Source: Ryujinx (MIT), see
 * THIRD_PARTY_NOTICES.md. */
void mii_charinfo_init_default(mii_charinfo_t *ci, int gender)
{
    memset(ci, 0, sizeof(*ci));
    mii_name_set_utf8(ci, "no name");
    ci->gender = gender ? 1 : 0;
    ci->height = 64;
    ci->build = 64;
    ci->hair_type = gender ? 12 : 33;
    ci->hair_color = 1;
    ci->eye_type = gender ? 4 : 2;
    ci->eye_color = 8;
    ci->eye_scale = 4;
    ci->eye_aspect = 3;
    ci->eye_rotate = gender ? 3 : 4;
    ci->eye_x = 2;
    ci->eye_y = 12;
    ci->eyebrow_type = gender ? 0 : 6;
    ci->eyebrow_color = 1;
    ci->eyebrow_scale = 4;
    ci->eyebrow_aspect = 3;
    ci->eyebrow_rotate = 6;
    ci->eyebrow_x = 2;
    ci->eyebrow_y = 10;
    ci->nose_type = 1;
    ci->nose_scale = 4;
    ci->nose_y = 9;
    ci->mouth_type = 23;
    ci->mouth_color = 19;
    ci->mouth_scale = 4;
    ci->mouth_aspect = 3;
    ci->mouth_y = 13;
    ci->beard_color = 8;
    ci->mustache_scale = 4;
    ci->mustache_y = 10;
    ci->glass_color = 8;
    ci->glass_scale = 4;
    ci->glass_y = 10;
    ci->mole_scale = 4;
    ci->mole_x = 2;
    ci->mole_y = 20;
}
