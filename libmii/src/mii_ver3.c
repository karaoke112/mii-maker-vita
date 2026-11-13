/* Ver3StoreData (0x60): the 3DS / Wii U / amiibo Mii format.
 *
 * Layout (multi-byte words little-endian, bit 0 = LSB):
 *   0x00 u8   version (3)
 *   0x01 u8   b0 copyable, b1 profanity, b2-3 region move, b4-5 font region
 *   0x02 u8   b0-3 page index, b4-7 slot index
 *   0x03 u8   b4-6 birth platform
 *   0x04 u8[8] author ("system") id
 *   0x0C u32  Mii id, BIG-endian. b31: 1 normal, 0 special
 *   0x10 u8[6] creator MAC
 *   0x18 u16  b0 gender, b1-4 birth month, b5-9 birth day, b10-13 fav color, b14 favorite
 *   0x1A u16[10] name
 *   0x2E u8   height      0x2F u8 build
 *   0x30 u8   b0 local only, b1-4 faceline type, b5-7 faceline color
 *   0x31 u8   b0-3 wrinkle, b4-7 makeup
 *   0x32 u8   hair type
 *   0x33 u8   b0-2 hair color, b3 flip
 *   0x34 u32  eye:     type 0-5, color 6-8, scale 9-12, aspect 13-15, rotate 16-20, x 21-24, y 25-29
 *   0x38 u32  eyebrow: type 0-4, color 5-7, scale 8-11, aspect 12-14, rotate 16-19, x 21-24, y 25-29
 *   0x3C u16  nose:    type 0-4, scale 5-8, y 9-13
 *   0x3E u16  mouth:   type 0-5, color 6-8, scale 9-12, aspect 13-15
 *   0x40 u16  mouth y 0-4, mustache type 5-7
 *   0x42 u16  beard type 0-2, beard color 3-5, mustache scale 6-9, mustache y 10-14
 *   0x44 u16  glass:   type 0-3, color 4-6, scale 7-10, y 11-15
 *   0x46 u16  mole:    on 0, scale 1-4, x 5-9, y 10-14
 *   0x48 u16[10] creator name
 *   0x5E u16  CRC16 of 0x00-0x5D, BIG-endian
 */
#include "mii_internal.h"

#include "mii_ver3_tables.inc"

#define V3_CORE_SIZE 0x5C
#define V3_CRC_OFS   0x5E

/* Ver3 -> Switch palette (nn::mii's own tables). */
static const uint8_t k_from_ver3_hair_color[8]  = { 8, 1, 2, 3, 4, 5, 6, 7 };
static const uint8_t k_from_ver3_eye_color[6]   = { 8, 9, 10, 11, 12, 13 };
static const uint8_t k_from_ver3_mouth_color[5] = { 19, 20, 21, 22, 23 };
static const uint8_t k_from_ver3_glass_color[6] = { 8, 14, 15, 16, 17, 18 };

uint8_t mii_ver3_hair_color_to_common(uint8_t v)  { return v < 8 ? k_from_ver3_hair_color[v] : 8; }
uint8_t mii_ver3_eye_color_to_common(uint8_t v)   { return v < 6 ? k_from_ver3_eye_color[v] : 8; }
uint8_t mii_ver3_mouth_color_to_common(uint8_t v) { return v < 5 ? k_from_ver3_mouth_color[v] : 19; }
uint8_t mii_ver3_glass_color_to_common(uint8_t v) { return v < 6 ? k_from_ver3_glass_color[v] : 0; }

uint8_t mii_common_to_ver3_hair_color(uint8_t c)  { return c < 100 ? k_to_ver3_hair_color[c] : 0; }
uint8_t mii_common_to_ver3_eye_color(uint8_t c)   { return c < 100 ? k_to_ver3_eye_color[c] : 0; }
uint8_t mii_common_to_ver3_mouth_color(uint8_t c) { return c < 100 ? k_to_ver3_mouth_color[c] : 0; }
uint8_t mii_common_to_ver3_glass_color(uint8_t c) { return c < 100 ? k_to_ver3_glass_color[c] : 0; }
uint8_t mii_faceline_color_to_ver3(uint8_t c)     { return c < 10 ? k_to_ver3_faceline_color[c] : 0; }
uint8_t mii_glass_type_to_ver3(uint8_t t)
{
    if (t <= 8)
        return t;
    return t <= 19 ? k_to_ver3_glass_type_hi[t - 9] : 0;
}

void mii_ver3_info_init_default(mii_ver3_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->copyable = 1;
    info->birth_platform = 4;
    info->mii_id = 0x80000000u;
}

mii_result_t mii_ver3_encode(const mii_charinfo_t *ci, const mii_ver3_info_t *info,
                             uint8_t out[MII_VER3_STOREDATA_SIZE])
{
    mii_ver3_info_t def;
    uint32_t w;

    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    if (!mii_fields_in_range(ci))
        return MII_ERR_INVALID;
    if (!info) {
        mii_ver3_info_init_default(&def);
        info = &def;
    }
    memset(out, 0, MII_VER3_STOREDATA_SIZE);

    out[0x00] = 3;
    out[0x01] = (uint8_t)((info->copyable & 1) | ((info->profanity & 1) << 1) |
                          ((ci->region_move & 3) << 2) | ((ci->font_region & 3) << 4));
    out[0x02] = (uint8_t)((info->page_index & 0xF) | ((info->slot_index & 0xF) << 4));
    out[0x03] = (uint8_t)((info->birth_platform & 7) << 4);
    memcpy(out + 0x04, info->author_id, 8);
    w = info->mii_id;
    w = ci->type ? (w & 0x7FFFFFFFu) : (w | 0x80000000u);
    mii_wr32be(out + 0x0C, w);
    memcpy(out + 0x10, info->creator_mac, 6);

    mii_wr16le(out + 0x18, (uint16_t)((ci->gender & 1) | ((info->birth_month & 0xF) << 1) |
                                      ((info->birth_day & 0x1F) << 5) |
                                      ((ci->favorite_color & 0xF) << 10) |
                                      ((info->favorite & 1) << 14)));
    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16le(out + 0x1A + 2 * i, ci->name[i]);
    out[0x2E] = ci->height;
    out[0x2F] = ci->build;
    out[0x30] = (uint8_t)((info->local_only & 1) | ((ci->faceline_type & 0xF) << 1) |
                          ((mii_faceline_color_to_ver3(ci->faceline_color) & 7) << 5));
    out[0x31] = (uint8_t)((ci->faceline_wrinkle & 0xF) | ((ci->faceline_make & 0xF) << 4));
    out[0x32] = ci->hair_type;
    out[0x33] = (uint8_t)((mii_common_to_ver3_hair_color(ci->hair_color) & 7) | ((ci->hair_flip & 1) << 3));

    w = 0;
    w = mii_put_bits(w, 0, 6, ci->eye_type);
    w = mii_put_bits(w, 6, 3, mii_common_to_ver3_eye_color(ci->eye_color));
    w = mii_put_bits(w, 9, 4, ci->eye_scale);
    w = mii_put_bits(w, 13, 3, ci->eye_aspect);
    w = mii_put_bits(w, 16, 5, ci->eye_rotate);
    w = mii_put_bits(w, 21, 4, ci->eye_x);
    w = mii_put_bits(w, 25, 5, ci->eye_y);
    mii_wr32le(out + 0x34, w);

    w = 0;
    w = mii_put_bits(w, 0, 5, ci->eyebrow_type);
    w = mii_put_bits(w, 5, 3, mii_common_to_ver3_hair_color(ci->eyebrow_color));
    w = mii_put_bits(w, 8, 4, ci->eyebrow_scale);
    w = mii_put_bits(w, 12, 3, ci->eyebrow_aspect);
    w = mii_put_bits(w, 16, 4, ci->eyebrow_rotate);
    w = mii_put_bits(w, 21, 4, ci->eyebrow_x);
    w = mii_put_bits(w, 25, 5, ci->eyebrow_y);
    mii_wr32le(out + 0x38, w);

    w = 0;
    w = mii_put_bits(w, 0, 5, ci->nose_type);
    w = mii_put_bits(w, 5, 4, ci->nose_scale);
    w = mii_put_bits(w, 9, 5, ci->nose_y);
    mii_wr16le(out + 0x3C, (uint16_t)w);

    w = 0;
    w = mii_put_bits(w, 0, 6, ci->mouth_type);
    w = mii_put_bits(w, 6, 3, mii_common_to_ver3_mouth_color(ci->mouth_color));
    w = mii_put_bits(w, 9, 4, ci->mouth_scale);
    w = mii_put_bits(w, 13, 3, ci->mouth_aspect);
    mii_wr16le(out + 0x3E, (uint16_t)w);

    w = 0;
    w = mii_put_bits(w, 0, 5, ci->mouth_y);
    w = mii_put_bits(w, 5, 3, ci->mustache_type);
    mii_wr16le(out + 0x40, (uint16_t)w);

    w = 0;
    w = mii_put_bits(w, 0, 3, ci->beard_type);
    w = mii_put_bits(w, 3, 3, mii_common_to_ver3_hair_color(ci->beard_color));
    w = mii_put_bits(w, 6, 4, ci->mustache_scale);
    w = mii_put_bits(w, 10, 5, ci->mustache_y);
    mii_wr16le(out + 0x42, (uint16_t)w);

    w = 0;
    w = mii_put_bits(w, 0, 4, mii_glass_type_to_ver3(ci->glass_type));
    w = mii_put_bits(w, 4, 3, mii_common_to_ver3_glass_color(ci->glass_color));
    w = mii_put_bits(w, 7, 4, ci->glass_scale);
    w = mii_put_bits(w, 11, 5, ci->glass_y);
    mii_wr16le(out + 0x44, (uint16_t)w);

    w = 0;
    w = mii_put_bits(w, 0, 1, ci->mole_type);
    w = mii_put_bits(w, 1, 4, ci->mole_scale);
    w = mii_put_bits(w, 5, 5, ci->mole_x);
    w = mii_put_bits(w, 10, 5, ci->mole_y);
    mii_wr16le(out + 0x46, (uint16_t)w);

    for (int i = 0; i < MII_NAME_LENGTH; i++)
        mii_wr16le(out + 0x48 + 2 * i, info->creator_name[i]);

    mii_wr16be(out + V3_CRC_OFS, mii_crc16(out, V3_CRC_OFS, 0));
    return MII_OK;
}

mii_result_t mii_ver3_decode(mii_charinfo_t *ci, mii_ver3_info_t *info,
                             const uint8_t *data, size_t size,
                             const mii_create_id_t *new_id)
{
    mii_ver3_info_t scratch;
    uint32_t w;

    if (!ci || !data)
        return MII_ERR_ARGUMENT;
    /* 0x5C is the same data without padding/CRC (".ffcd/.cfcd"). */
    if (size != MII_VER3_STOREDATA_SIZE && size != V3_CORE_SIZE)
        return MII_ERR_SIZE;
    if (size == MII_VER3_STOREDATA_SIZE && mii_crc16(data, MII_VER3_STOREDATA_SIZE, 0) != 0)
        return MII_ERR_CRC;
    if (!info)
        info = &scratch;

    memset(ci, 0, sizeof(*ci));
    memset(info, 0, sizeof(*info));
    if (new_id)
        ci->create_id = *new_id;

    info->copyable   = data[0x01] & 1;
    info->profanity  = (data[0x01] >> 1) & 1;
    ci->region_move  = (data[0x01] >> 2) & 3;
    ci->font_region  = (data[0x01] >> 4) & 3;
    info->page_index = data[0x02] & 0xF;
    info->slot_index = data[0x02] >> 4;
    info->birth_platform = (data[0x03] >> 4) & 7;
    memcpy(info->author_id, data + 0x04, 8);
    info->mii_id = mii_rd32be(data + 0x0C);
    ci->type = (info->mii_id & 0x80000000u) ? 0 : 1;
    memcpy(info->creator_mac, data + 0x10, 6);

    w = mii_rd16le(data + 0x18);
    ci->gender          = (uint8_t)mii_bits(w, 0, 1);
    info->birth_month   = (uint8_t)mii_bits(w, 1, 4);
    info->birth_day     = (uint8_t)mii_bits(w, 5, 5);
    ci->favorite_color  = (uint8_t)mii_bits(w, 10, 4);
    info->favorite      = (uint8_t)mii_bits(w, 14, 1);
    mii_read_name(ci->name, data + 0x1A);
    ci->height = data[0x2E];
    ci->build  = data[0x2F];

    info->local_only     = data[0x30] & 1;
    ci->faceline_type    = (data[0x30] >> 1) & 0xF;
    ci->faceline_color   = (data[0x30] >> 5) & 7;   /* Ver3 0-5 == Switch 0-5 */
    ci->faceline_wrinkle = data[0x31] & 0xF;
    ci->faceline_make    = data[0x31] >> 4;
    ci->hair_type        = data[0x32];
    ci->hair_color       = mii_ver3_hair_color_to_common(data[0x33] & 7);
    ci->hair_flip        = (data[0x33] >> 3) & 1;

    w = mii_rd32le(data + 0x34);
    ci->eye_type   = (uint8_t)mii_bits(w, 0, 6);
    ci->eye_color  = mii_ver3_eye_color_to_common((uint8_t)mii_bits(w, 6, 3));
    ci->eye_scale  = (uint8_t)mii_bits(w, 9, 4);
    ci->eye_aspect = (uint8_t)mii_bits(w, 13, 3);
    ci->eye_rotate = (uint8_t)mii_bits(w, 16, 5);
    ci->eye_x      = (uint8_t)mii_bits(w, 21, 4);
    ci->eye_y      = (uint8_t)mii_bits(w, 25, 5);

    w = mii_rd32le(data + 0x38);
    ci->eyebrow_type   = (uint8_t)mii_bits(w, 0, 5);
    ci->eyebrow_color  = mii_ver3_hair_color_to_common((uint8_t)mii_bits(w, 5, 3));
    ci->eyebrow_scale  = (uint8_t)mii_bits(w, 8, 4);
    ci->eyebrow_aspect = (uint8_t)mii_bits(w, 12, 3);
    ci->eyebrow_rotate = (uint8_t)mii_bits(w, 16, 4);
    ci->eyebrow_x      = (uint8_t)mii_bits(w, 21, 4);
    ci->eyebrow_y      = (uint8_t)mii_bits(w, 25, 5);

    w = mii_rd16le(data + 0x3C);
    ci->nose_type  = (uint8_t)mii_bits(w, 0, 5);
    ci->nose_scale = (uint8_t)mii_bits(w, 5, 4);
    ci->nose_y     = (uint8_t)mii_bits(w, 9, 5);

    w = mii_rd16le(data + 0x3E);
    ci->mouth_type   = (uint8_t)mii_bits(w, 0, 6);
    ci->mouth_color  = mii_ver3_mouth_color_to_common((uint8_t)mii_bits(w, 6, 3));
    ci->mouth_scale  = (uint8_t)mii_bits(w, 9, 4);
    ci->mouth_aspect = (uint8_t)mii_bits(w, 13, 3);

    w = mii_rd16le(data + 0x40);
    ci->mouth_y       = (uint8_t)mii_bits(w, 0, 5);
    ci->mustache_type = (uint8_t)mii_bits(w, 5, 3);

    w = mii_rd16le(data + 0x42);
    ci->beard_type     = (uint8_t)mii_bits(w, 0, 3);
    ci->beard_color    = mii_ver3_hair_color_to_common((uint8_t)mii_bits(w, 3, 3));
    ci->mustache_scale = (uint8_t)mii_bits(w, 6, 4);
    ci->mustache_y     = (uint8_t)mii_bits(w, 10, 5);

    w = mii_rd16le(data + 0x44);
    ci->glass_type  = (uint8_t)mii_bits(w, 0, 4);
    ci->glass_color = mii_ver3_glass_color_to_common((uint8_t)mii_bits(w, 4, 3));
    ci->glass_scale = (uint8_t)mii_bits(w, 7, 4);
    ci->glass_y     = (uint8_t)mii_bits(w, 11, 5);

    w = mii_rd16le(data + 0x46);
    ci->mole_type  = (uint8_t)mii_bits(w, 0, 1);
    ci->mole_scale = (uint8_t)mii_bits(w, 1, 4);
    ci->mole_x     = (uint8_t)mii_bits(w, 5, 5);
    ci->mole_y     = (uint8_t)mii_bits(w, 10, 5);

    mii_read_name(info->creator_name, data + 0x48);

    return mii_fields_in_range(ci) ? MII_OK : MII_ERR_INVALID;
}
