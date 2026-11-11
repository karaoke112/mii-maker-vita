/*
 * libmii - portable C99 library for official Nintendo Mii data formats.
 *
 * Canonical in-memory form is mii_charinfo_t, a byte-per-field mirror of the
 * Nintendo Switch "CharInfo" (nn::mii::CharInfo). Everything else converts to
 * and from it:
 *
 *   .charinfo  0x58  Switch CharInfo, used by Switch games            (lossless)
 *   .nfcd      0x30  Switch CoreData, bit-packed CharInfo, no ID       (lossless)
 *   .nfsd      0x44  Switch StoreData = CoreData + CreateId + 2 CRC16  (lossless)
 *   .nfdb    0x1A98  Switch Mii database (NFDB), 100 StoreData slots
 *   .ffsd/.cfsd 0x60 Ver3StoreData (3DS / Wii U / amiibo)  (lossy from Switch:
 *                    100 colors -> 6-8 colors, 20 glasses -> 9)
 *
 * No allocation, no I/O, no globals: safe to embed in a Vita app, a game port
 * or a PC tool. All multi-byte values are serialized explicitly, so the code
 * does not depend on host endianness or struct packing.
 */
#ifndef MII_MII_H
#define MII_MII_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MII_CHARINFO_SIZE        0x58
#define MII_COREDATA_SIZE        0x30
#define MII_STOREDATA_SIZE       0x44
#define MII_VER3_STOREDATA_SIZE  0x60
#define MII_NFDB_SIZE            0x1A98
#define MII_NFDB_MAX_MIIS        100

#define MII_NAME_LENGTH          10   /* UTF-16 code units, excluding terminator */
#define MII_COMMON_COLOR_COUNT   100  /* Switch hair/eye/mouth/glasses palette   */

typedef enum mii_result {
    MII_OK = 0,
    MII_ERR_ARGUMENT  = -1, /* NULL pointer or bad parameter                 */
    MII_ERR_SIZE      = -2, /* buffer has the wrong length                   */
    MII_ERR_CRC       = -3, /* checksum mismatch                             */
    MII_ERR_INVALID   = -4, /* a field is out of range (see mii_verify)      */
    MII_ERR_MAGIC     = -5, /* database magic / version mismatch             */
    MII_ERR_FULL      = -6, /* database has no free slot                     */
    MII_ERR_NOT_FOUND = -7
} mii_result_t;

const char *mii_result_string(mii_result_t r);

/* ------------------------------------------------------------------------- */
/* Create ID: a 16-byte RFC 4122 v4 style UUID that identifies a Mii.        */
/* ------------------------------------------------------------------------- */

typedef struct mii_create_id {
    uint8_t bytes[16];
} mii_create_id_t;

/* Turns 16 random bytes into a valid create id (sets the variant bits).
 * On Vita, fill `random16` with sceKernelGetRandomNumber(). */
void mii_create_id_from_random(mii_create_id_t *id, const uint8_t random16[16]);
int  mii_create_id_is_valid(const mii_create_id_t *id);
int  mii_create_id_is_null(const mii_create_id_t *id);
int  mii_create_id_equal(const mii_create_id_t *a, const mii_create_id_t *b);

/* ------------------------------------------------------------------------- */
/* CharInfo                                                                  */
/* ------------------------------------------------------------------------- */

/* Field order matches the CharInfo byte layout from offset 0x26 onwards. */
typedef enum mii_field {
    MII_FIELD_FONT_REGION,      /* 0-3   0 standard, 1 CHN, 2 KOR, 3 TWN      */
    MII_FIELD_FAVORITE_COLOR,   /* 0-11                                       */
    MII_FIELD_GENDER,           /* 0 male, 1 female                           */
    MII_FIELD_HEIGHT,           /* 0-127                                      */
    MII_FIELD_BUILD,            /* 0-127  ("weight")                          */
    MII_FIELD_TYPE,             /* 0 normal, 1 special                        */
    MII_FIELD_REGION_MOVE,      /* 0-3                                        */
    MII_FIELD_FACELINE_TYPE,    /* 0-11   face shape                          */
    MII_FIELD_FACELINE_COLOR,   /* 0-9    skin color                          */
    MII_FIELD_FACELINE_WRINKLE, /* 0-11                                       */
    MII_FIELD_FACELINE_MAKE,    /* 0-11   makeup                              */
    MII_FIELD_HAIR_TYPE,        /* 0-131                                      */
    MII_FIELD_HAIR_COLOR,       /* 0-99   common color                        */
    MII_FIELD_HAIR_FLIP,        /* 0-1                                        */
    MII_FIELD_EYE_TYPE,         /* 0-59                                       */
    MII_FIELD_EYE_COLOR,        /* 0-99                                       */
    MII_FIELD_EYE_SCALE,        /* 0-7                                        */
    MII_FIELD_EYE_ASPECT,       /* 0-6                                        */
    MII_FIELD_EYE_ROTATE,       /* 0-7                                        */
    MII_FIELD_EYE_X,            /* 0-12   distance apart                      */
    MII_FIELD_EYE_Y,            /* 0-18                                       */
    MII_FIELD_EYEBROW_TYPE,     /* 0-23                                       */
    MII_FIELD_EYEBROW_COLOR,    /* 0-99                                       */
    MII_FIELD_EYEBROW_SCALE,    /* 0-8                                        */
    MII_FIELD_EYEBROW_ASPECT,   /* 0-6                                        */
    MII_FIELD_EYEBROW_ROTATE,   /* 0-11                                       */
    MII_FIELD_EYEBROW_X,        /* 0-12                                       */
    MII_FIELD_EYEBROW_Y,        /* 3-18   (note: minimum is 3)                */
    MII_FIELD_NOSE_TYPE,        /* 0-17                                       */
    MII_FIELD_NOSE_SCALE,       /* 0-8                                        */
    MII_FIELD_NOSE_Y,           /* 0-18                                       */
    MII_FIELD_MOUTH_TYPE,       /* 0-35                                       */
    MII_FIELD_MOUTH_COLOR,      /* 0-99                                       */
    MII_FIELD_MOUTH_SCALE,      /* 0-8                                        */
    MII_FIELD_MOUTH_ASPECT,     /* 0-6                                        */
    MII_FIELD_MOUTH_Y,          /* 0-18                                       */
    MII_FIELD_BEARD_COLOR,      /* 0-99                                       */
    MII_FIELD_BEARD_TYPE,       /* 0-5                                        */
    MII_FIELD_MUSTACHE_TYPE,    /* 0-5                                        */
    MII_FIELD_MUSTACHE_SCALE,   /* 0-8                                        */
    MII_FIELD_MUSTACHE_Y,       /* 0-16                                       */
    MII_FIELD_GLASS_TYPE,       /* 0-19                                       */
    MII_FIELD_GLASS_COLOR,      /* 0-99                                       */
    MII_FIELD_GLASS_SCALE,      /* 0-7                                        */
    MII_FIELD_GLASS_Y,          /* 0-20                                       */
    MII_FIELD_MOLE_TYPE,        /* 0-1                                        */
    MII_FIELD_MOLE_SCALE,       /* 0-8                                        */
    MII_FIELD_MOLE_X,           /* 0-16                                       */
    MII_FIELD_MOLE_Y,           /* 0-30                                       */
    MII_FIELD_COUNT
} mii_field_t;

typedef struct mii_charinfo {
    mii_create_id_t create_id;
    uint16_t name[MII_NAME_LENGTH + 1]; /* UTF-16 code units, 0-terminated */
    uint8_t font_region;
    uint8_t favorite_color;
    uint8_t gender;
    uint8_t height;
    uint8_t build;
    uint8_t type;
    uint8_t region_move;
    uint8_t faceline_type;
    uint8_t faceline_color;
    uint8_t faceline_wrinkle;
    uint8_t faceline_make;
    uint8_t hair_type;
    uint8_t hair_color;
    uint8_t hair_flip;
    uint8_t eye_type;
    uint8_t eye_color;
    uint8_t eye_scale;
    uint8_t eye_aspect;
    uint8_t eye_rotate;
    uint8_t eye_x;
    uint8_t eye_y;
    uint8_t eyebrow_type;
    uint8_t eyebrow_color;
    uint8_t eyebrow_scale;
    uint8_t eyebrow_aspect;
    uint8_t eyebrow_rotate;
    uint8_t eyebrow_x;
    uint8_t eyebrow_y;
    uint8_t nose_type;
    uint8_t nose_scale;
    uint8_t nose_y;
    uint8_t mouth_type;
    uint8_t mouth_color;
    uint8_t mouth_scale;
    uint8_t mouth_aspect;
    uint8_t mouth_y;
    uint8_t beard_color;
    uint8_t beard_type;
    uint8_t mustache_type;
    uint8_t mustache_scale;
    uint8_t mustache_y;
    uint8_t glass_type;
    uint8_t glass_color;
    uint8_t glass_scale;
    uint8_t glass_y;
    uint8_t mole_type;
    uint8_t mole_scale;
    uint8_t mole_x;
    uint8_t mole_y;
    uint8_t reserved;
} mii_charinfo_t;

/* Generic field access, so an editor can drive every slider/grid from one
 * table instead of 49 hand-written cases. */
const char *mii_field_name(mii_field_t field);
uint8_t     mii_field_min(mii_field_t field);
uint8_t     mii_field_max(mii_field_t field);
uint8_t     mii_field_get(const mii_charinfo_t *ci, mii_field_t field);
/* Clamps to [min, max]. Returns the stored value. */
uint8_t     mii_field_set(mii_charinfo_t *ci, mii_field_t field, int value);

/* Base "start from scratch" Mii, the same templates nn::mii uses.
 * gender: 0 male, 1 female. The create id is left zero: set one before
 * saving (mii_create_id_from_random). */
void mii_charinfo_init_default(mii_charinfo_t *ci, int gender);

/* Returns MII_FIELD_COUNT if every field is in range and the name is a
 * valid, non-empty UTF-16 string; otherwise the first offending field
 * (or MII_FIELD_COUNT + 1 for a bad name, MII_FIELD_COUNT + 2 for a bad
 * create id when check_create_id is set). */
int mii_verify(const mii_charinfo_t *ci, int check_create_id);

/* Name helpers (UTF-8 <-> CharInfo UTF-16). Return code units / bytes written. */
size_t mii_name_set_utf8(mii_charinfo_t *ci, const char *utf8);
size_t mii_name_get_utf8(const mii_charinfo_t *ci, char *out, size_t out_size);

mii_result_t mii_charinfo_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size);
mii_result_t mii_charinfo_encode(const mii_charinfo_t *ci, uint8_t out[MII_CHARINFO_SIZE]);

/* ------------------------------------------------------------------------- */
/* Switch CoreData / StoreData                                               */
/* ------------------------------------------------------------------------- */

/* CoreData carries no create id: decode leaves ci->create_id zeroed. */
mii_result_t mii_coredata_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size);
mii_result_t mii_coredata_encode(const mii_charinfo_t *ci, uint8_t out[MII_COREDATA_SIZE]);

/* StoreData has two CRC16s: one over the data and one seeded with a CRC of
 * the console's 16-byte Mii author id. Pass the author id you want the
 * device CRC computed with (a per-Vita random id is fine; real Switch tools
 * such as MiiPort recompute it on import). author_id may be NULL to write
 * a device CRC for an all-zero id. */
mii_result_t mii_storedata_encode(const mii_charinfo_t *ci, const uint8_t author_id[16],
                                  uint8_t out[MII_STOREDATA_SIZE]);
/* Verifies the data CRC. If author_id is non-NULL the device CRC is checked
 * too. */
mii_result_t mii_storedata_decode(mii_charinfo_t *ci, const uint8_t *data, size_t size,
                                  const uint8_t author_id[16]);

/* ------------------------------------------------------------------------- */
/* Ver3StoreData (3DS / Wii U / amiibo / Mii QR codes after decryption)      */
/* ------------------------------------------------------------------------- */

/* Data Ver3 has that CharInfo does not. Pass NULL to use defaults. */
typedef struct mii_ver3_info {
    uint8_t  copyable;         /* allow other consoles to copy               */
    uint8_t  profanity;        /* name flagged as profane                    */
    uint8_t  local_only;       /* "don't share"                              */
    uint8_t  favorite;
    uint8_t  page_index;       /* Mii Maker page/slot on 3DS                 */
    uint8_t  slot_index;
    uint8_t  birth_platform;   /* 1 Wii, 2 DS, 3 3DS, 4 Wii U / Switch       */
    uint8_t  birth_month;      /* 0 = unset, 1-12                            */
    uint8_t  birth_day;        /* 0 = unset, 1-31                            */
    uint8_t  author_id[8];     /* "system id"                                */
    uint32_t mii_id;           /* bit 31: 1 normal / 0 special; bits 0-27:   */
                               /* creation time in 2 s units since 2010      */
    uint8_t  creator_mac[6];
    uint16_t creator_name[MII_NAME_LENGTH + 1];
} mii_ver3_info_t;

void mii_ver3_info_init_default(mii_ver3_info_t *info);

/* Converts colors and glasses down to the Ver3 palette (lossy). */
mii_result_t mii_ver3_encode(const mii_charinfo_t *ci, const mii_ver3_info_t *info,
                             uint8_t out[MII_VER3_STOREDATA_SIZE]);
/* info may be NULL. create_id is not stored in Ver3: ci->create_id is set to
 * `new_id` if given, else zeroed. */
mii_result_t mii_ver3_decode(mii_charinfo_t *ci, mii_ver3_info_t *info,
                             const uint8_t *data, size_t size,
                             const mii_create_id_t *new_id);

/* Palette conversion used by the two functions above. */
uint8_t mii_ver3_hair_color_to_common(uint8_t ver3);    /* also eyebrow/beard */
uint8_t mii_ver3_eye_color_to_common(uint8_t ver3);
uint8_t mii_ver3_mouth_color_to_common(uint8_t ver3);
uint8_t mii_ver3_glass_color_to_common(uint8_t ver3);
uint8_t mii_common_to_ver3_hair_color(uint8_t common);
uint8_t mii_common_to_ver3_eye_color(uint8_t common);
uint8_t mii_common_to_ver3_mouth_color(uint8_t common);
uint8_t mii_common_to_ver3_glass_color(uint8_t common);
uint8_t mii_faceline_color_to_ver3(uint8_t switch_color);
uint8_t mii_glass_type_to_ver3(uint8_t switch_type);

/* ------------------------------------------------------------------------- */
/* NFDB: the Switch's own Mii database file, used as the shared Vita store   */
/* ------------------------------------------------------------------------- */

typedef struct mii_nfdb {
    uint8_t  count;
    uint8_t  version;
    uint8_t  entries[MII_NFDB_MAX_MIIS][MII_STOREDATA_SIZE]; /* raw StoreData */
} mii_nfdb_t;

void         mii_nfdb_init(mii_nfdb_t *db);
mii_result_t mii_nfdb_decode(mii_nfdb_t *db, const uint8_t *data, size_t size);
mii_result_t mii_nfdb_encode(const mii_nfdb_t *db, uint8_t out[MII_NFDB_SIZE]);
mii_result_t mii_nfdb_get(const mii_nfdb_t *db, int index, mii_charinfo_t *ci);
/* Adds, or replaces the entry with the same create id. Returns the index
 * (>= 0) or a negative mii_result_t. */
int          mii_nfdb_put(mii_nfdb_t *db, const mii_charinfo_t *ci, const uint8_t author_id[16]);
int          mii_nfdb_find(const mii_nfdb_t *db, const mii_create_id_t *id);
mii_result_t mii_nfdb_remove(mii_nfdb_t *db, int index);
mii_result_t mii_nfdb_move(mii_nfdb_t *db, int from, int to);

/* ------------------------------------------------------------------------- */
/* Utilities                                                                 */
/* ------------------------------------------------------------------------- */

/* CRC-16/XMODEM (poly 0x1021, init `seed`), as used by every Mii format. */
uint16_t mii_crc16(const uint8_t *data, size_t size, uint16_t seed);

#ifdef __cplusplus
}
#endif

#endif /* MII_MII_H */
