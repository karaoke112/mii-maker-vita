/* Switch formats: CharInfo (0x58), CoreData (0x30), StoreData (0x44). */
#include "mii_internal.h"

/* ---- CharInfo ---------------------------------------------------------- */

#define CI_NAME_OFS   0x10
#define CI_FIELD_OFS  0x26  /* first of the 49 one-byte fields */
#define CI_RESERVED   0x57

mii_result_t mii_charinfo_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size)
{
    if (!ci || !data)
        return MII_ERR_ARGUMENT;
    if (size != MII_CHARINFO_SIZE)
        return MII_ERR_SIZE;
    memset(ci, 0, sizeof(*ci));
    memcpy(ci->create_id.bytes, data, 16);
    mii_read_name(ci->name, data + CI_NAME_OFS);
    for (int f = 0; f < MII_FIELD_COUNT; f++)
        *((uint8_t *)ci + offsetof(mii_charinfo_t, font_region) + f) = data[CI_FIELD_OFS + f];
    ci->reserved = data[CI_RESERVED];
    return mii_fields_in_range(ci) ? MII_OK : MII_ERR_INVALID;
}

mii_result_t mii_charinfo_encode(const mii_charinfo_t *ci, uint8_t out[MII_CHARINFO_SIZE])
{
    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    if (!mii_fields_in_range(ci))
        return MII_ERR_INVALID;
    memset(out, 0, MII_CHARINFO_SIZE);
    memcpy(out, ci->create_id.bytes, 16);
    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16le(out + CI_NAME_OFS + 2 * i, ci->name[i]);
    /* name[10] is the terminator and is always written as 0 */
    for (int f = 0; f < MII_FIELD_COUNT; f++)
        out[CI_FIELD_OFS + f] = *((const uint8_t *)ci + offsetof(mii_charinfo_t, font_region) + f);
    out[CI_RESERVED] = 0;
    return MII_OK;
}

/* ---- CoreData ---------------------------------------------------------- */

/* Where each field lives in the 0x30-byte CoreData. Values are stored as
 * (value - min) in `width` bits at `bit` of byte `byte`. */
typedef struct core_slot {
    uint8_t byte, bit, width;
} core_slot;

static const core_slot k_core[MII_FIELD_COUNT] = {
    [MII_FIELD_HAIR_TYPE]        = { 0x00, 0, 8 },
    [MII_FIELD_HEIGHT]           = { 0x01, 0, 7 },
    [MII_FIELD_MOLE_TYPE]        = { 0x01, 7, 1 },
    [MII_FIELD_BUILD]            = { 0x02, 0, 7 },
    [MII_FIELD_HAIR_FLIP]        = { 0x02, 7, 1 },
    [MII_FIELD_HAIR_COLOR]       = { 0x03, 0, 7 },
    [MII_FIELD_TYPE]             = { 0x03, 7, 1 },
    [MII_FIELD_EYE_COLOR]        = { 0x04, 0, 7 },
    [MII_FIELD_GENDER]           = { 0x04, 7, 1 },
    [MII_FIELD_EYEBROW_COLOR]    = { 0x05, 0, 7 },
    [MII_FIELD_MOUTH_COLOR]      = { 0x06, 0, 7 },
    [MII_FIELD_BEARD_COLOR]      = { 0x07, 0, 7 },
    [MII_FIELD_GLASS_COLOR]      = { 0x08, 0, 7 },
    [MII_FIELD_EYE_TYPE]         = { 0x09, 0, 6 },
    [MII_FIELD_REGION_MOVE]      = { 0x09, 6, 2 },
    [MII_FIELD_MOUTH_TYPE]       = { 0x0A, 0, 6 },
    [MII_FIELD_FONT_REGION]      = { 0x0A, 6, 2 },
    [MII_FIELD_EYE_Y]            = { 0x0B, 0, 5 },
    [MII_FIELD_GLASS_SCALE]      = { 0x0B, 5, 3 },
    [MII_FIELD_EYEBROW_TYPE]     = { 0x0C, 0, 5 },
    [MII_FIELD_MUSTACHE_TYPE]    = { 0x0C, 5, 3 },
    [MII_FIELD_NOSE_TYPE]        = { 0x0D, 0, 5 },
    [MII_FIELD_BEARD_TYPE]       = { 0x0D, 5, 3 },
    [MII_FIELD_NOSE_Y]           = { 0x0E, 0, 5 },
    [MII_FIELD_MOUTH_ASPECT]     = { 0x0E, 5, 3 },
    [MII_FIELD_MOUTH_Y]          = { 0x0F, 0, 5 },
    [MII_FIELD_EYEBROW_ASPECT]   = { 0x0F, 5, 3 },
    [MII_FIELD_MUSTACHE_Y]       = { 0x10, 0, 5 },
    [MII_FIELD_EYE_ROTATE]       = { 0x10, 5, 3 },
    [MII_FIELD_GLASS_Y]          = { 0x11, 0, 5 },
    [MII_FIELD_EYE_ASPECT]       = { 0x11, 5, 3 },
    [MII_FIELD_MOLE_X]           = { 0x12, 0, 5 },
    [MII_FIELD_EYE_SCALE]        = { 0x12, 5, 3 },
    [MII_FIELD_MOLE_Y]           = { 0x13, 0, 5 },
    [MII_FIELD_GLASS_TYPE]       = { 0x14, 0, 5 },
    [MII_FIELD_FAVORITE_COLOR]   = { 0x15, 0, 4 },
    [MII_FIELD_FACELINE_TYPE]    = { 0x15, 4, 4 },
    [MII_FIELD_FACELINE_COLOR]   = { 0x16, 0, 4 },
    [MII_FIELD_FACELINE_WRINKLE] = { 0x16, 4, 4 },
    [MII_FIELD_FACELINE_MAKE]    = { 0x17, 0, 4 },
    [MII_FIELD_EYE_X]            = { 0x17, 4, 4 },
    [MII_FIELD_EYEBROW_SCALE]    = { 0x18, 0, 4 },
    [MII_FIELD_EYEBROW_ROTATE]   = { 0x18, 4, 4 },
    [MII_FIELD_EYEBROW_X]        = { 0x19, 0, 4 },
    [MII_FIELD_EYEBROW_Y]        = { 0x19, 4, 4 },
    [MII_FIELD_NOSE_SCALE]       = { 0x1A, 0, 4 },
    [MII_FIELD_MOUTH_SCALE]      = { 0x1A, 4, 4 },
    [MII_FIELD_MUSTACHE_SCALE]   = { 0x1B, 0, 4 },
    [MII_FIELD_MOLE_SCALE]       = { 0x1B, 4, 4 },
};

#define CORE_NAME_OFS 0x1C

static void core_unpack(mii_charinfo_t *ci, const uint8_t *data)
{
    for (int f = 0; f < MII_FIELD_COUNT; f++) {
        const core_slot *s = &k_core[f];
        uint32_t raw = mii_bits(data[s->byte], s->bit, s->width);
        *((uint8_t *)ci + offsetof(mii_charinfo_t, font_region) + f) =
            (uint8_t)(raw + mii_field_min((mii_field_t)f));
    }
    mii_read_name(ci->name, data + CORE_NAME_OFS);
}

static void core_pack(const mii_charinfo_t *ci, uint8_t *out)
{
    memset(out, 0, MII_COREDATA_SIZE);
    for (int f = 0; f < MII_FIELD_COUNT; f++) {
        const core_slot *s = &k_core[f];
        uint8_t v = *((const uint8_t *)ci + offsetof(mii_charinfo_t, font_region) + f);
        v = (uint8_t)(v - mii_field_min((mii_field_t)f));
        out[s->byte] = (uint8_t)mii_put_bits(out[s->byte], s->bit, s->width, v);
    }
    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16le(out + CORE_NAME_OFS + 2 * i, ci->name[i]);
}

mii_result_t mii_coredata_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size)
{
    if (!ci || !data)
        return MII_ERR_ARGUMENT;
    if (size != MII_COREDATA_SIZE)
        return MII_ERR_SIZE;
    memset(ci, 0, sizeof(*ci));
    core_unpack(ci, data);
    return mii_fields_in_range(ci) ? MII_OK : MII_ERR_INVALID;
}

mii_result_t mii_coredata_encode(const mii_charinfo_t *ci, uint8_t out[MII_COREDATA_SIZE])
{
    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    if (!mii_fields_in_range(ci))
        return MII_ERR_INVALID;
    core_pack(ci, out);
    return MII_OK;
}

/* ---- StoreData --------------------------------------------------------- */

#define SD_CREATE_ID_OFS  0x30
#define SD_DATA_CRC_OFS   0x40
#define SD_DEVICE_CRC_OFS 0x42

static uint16_t device_seed(const uint8_t author_id[16])
{
    static const uint8_t zero[16] = { 0 };
    return mii_crc16(author_id ? author_id : zero, 16, 0);
}

mii_result_t mii_storedata_encode(const mii_charinfo_t *ci, const uint8_t author_id[16],
                                  uint8_t out[MII_STOREDATA_SIZE])
{
    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    if (!mii_fields_in_range(ci))
        return MII_ERR_INVALID;
    core_pack(ci, out);
    memcpy(out + SD_CREATE_ID_OFS, ci->create_id.bytes, 16);
    mii_wr16be(out + SD_DATA_CRC_OFS, mii_crc16(out, SD_DATA_CRC_OFS, 0));
    mii_wr16be(out + SD_DEVICE_CRC_OFS, mii_crc16(out, SD_DEVICE_CRC_OFS, device_seed(author_id)));
    return MII_OK;
}

mii_result_t mii_storedata_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size,
                                  const uint8_t author_id[16])
{
    if (!ci || !data)
        return MII_ERR_ARGUMENT;
    if (size != MII_STOREDATA_SIZE)
        return MII_ERR_SIZE;
    if (mii_crc16(data, SD_DEVICE_CRC_OFS, 0) != 0)
        return MII_ERR_CRC;
    if (author_id && mii_crc16(data, MII_STOREDATA_SIZE, device_seed(author_id)) != 0)
        return MII_ERR_CRC;
    memset(ci, 0, sizeof(*ci));
    core_unpack(ci, data);
    memcpy(ci->create_id.bytes, data + SD_CREATE_ID_OFS, 16);
    return mii_fields_in_range(ci) ? MII_OK : MII_ERR_INVALID;
}
