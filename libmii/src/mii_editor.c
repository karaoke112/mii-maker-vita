/* Editor-support data. See THIRD_PARTY_NOTICES.md for sources:
 *  - palettes: sRGB columns of nn::mii's CommonColorTable / FacelineColor /
 *    UpperLipColor tables as published (Unlicense) in Arian Kordi's FFL fork,
 *    converted to 8-bit.
 *  - grids: MiiJS data.js `lookupTables.grids` (ISC). Three duplicated
 *    entries in the color grid were corrected (58, 61 and 76 restored).
 */
#include "mii/mii_editor.h"

static const uint32_t k_common_colors[100] = {
    0x2D2828, 0x402010, 0x5C180A, 0x7C3A14, 0x787880, 0x4E3E10,
    0x885818, 0xD0A04A, 0x000000, 0x6C7070, 0x663C2C, 0x605E30,
    0x4654A8, 0x387058, 0x603810, 0xA81008, 0x203068, 0xA86000,
    0x787068, 0xD85208, 0xF00C08, 0xF54848, 0xF09A74, 0x8C5040,
    0x842626, 0xFF7366, 0xFFA6A6, 0xFFC0BA, 0x732E3B, 0x991F3D,
    0x8A173E, 0xB53E42, 0xC71E56, 0xB05381, 0xC7546E, 0xFA7597,
    0xFCACC9, 0xFFC9D8, 0x311C40, 0x37283D, 0x4C184D, 0x6F42B3,
    0x855CB8, 0xC083CC, 0xA893C9, 0xC5ACE6, 0xEEBEFA, 0xD2C5ED,
    0x191F40, 0x123F66, 0x2A82D4, 0x57B4F2, 0x7AC5DE, 0x89A6FA,
    0x84BDFA, 0xA1E3FF, 0x0B2E36, 0x013D3B, 0x0D4F59, 0x236663,
    0x307E8C, 0x4FAEB0, 0x7AC49E, 0x7FD4C0, 0x87E5B6, 0x0A4A35,
    0x437A00, 0x027562, 0x369970, 0x4BAD1A, 0x92BF0A, 0x63C788,
    0x9EE042, 0x96DE7E, 0xBBF2AA, 0x99932B, 0xA69563, 0xCCC039,
    0xCCB987, 0xD9CC82, 0xD5D96F, 0xD5E683, 0xD8FA9D, 0x7D4500,
    0xE6BB7A, 0xFEE24A, 0xFADE82, 0xF7EA9C, 0xFAF89B, 0xA64D1E,
    0xFF960D, 0xD19B69, 0xFFB266, 0xFFC28C, 0xE5CFB1, 0x414141,
    0x9B9B9B, 0xBEBEBE, 0xDCD7CD, 0xFFFFFF,
};
static const uint32_t k_faceline_colors[10] = {
    0xFFD3AD, 0xFFB66B, 0xDE7942, 0xFFAA8C, 0xAD5129, 0x632C18,
    0xFFBEA5, 0xFFC58F, 0x8C3C23, 0x3C2D23,
};
static const uint32_t k_upper_lip_colors[100] = {
    0x171414, 0x201008, 0x2E0C05, 0x4A230C, 0x54545A, 0x271F08,
    0x52350E, 0xB18028, 0x000000, 0x4C4E4E, 0x331E16, 0x3A381D,
    0x2A3265, 0x274E3E, 0x301C08, 0x650A05, 0x101834, 0x764300,
    0x544E49, 0x823018, 0x780C0C, 0x882028, 0xDC7850, 0x461E0A,
    0x4F1717, 0x99453D, 0xE68585, 0xE6A19B, 0x451C23, 0x5C1325,
    0x530E25, 0x6D2528, 0x771234, 0x6A324D, 0x773242, 0xAF526A,
    0xE38CAC, 0xE6ABBB, 0x190E20, 0x1C141F, 0x260C27, 0x43286B,
    0x50376E, 0x865C8F, 0x76678D, 0xAB90CF, 0xD4A0E1, 0xB8AAD5,
    0x0D1020, 0x092033, 0x1D5B94, 0x3297DA, 0x5CADC8, 0x6786E1,
    0x629FE1, 0x80C7E6, 0x06171B, 0x011F1E, 0x07282D, 0x184745,
    0x225862, 0x308B8D, 0x60B087, 0x63BFA9, 0x69CE9B, 0x05251B,
    0x284900, 0x01463B, 0x266B4E, 0x2C8A00, 0x6E9900, 0x47B36F,
    0x82CA1F, 0x7AC860, 0x9EDA8C, 0x6B671E, 0x746845, 0xA39816,
    0xB8A36D, 0xC3B565, 0xBFC351, 0xBDCF64, 0xBCE17D, 0x4B2900,
    0xCFA15A, 0xE5C622, 0xE1C35F, 0xDED07C, 0xE1DF7A, 0x642E12,
    0xCC780A, 0xBC824C, 0xE69240, 0xE6A469, 0xCEB696, 0x212121,
    0x7C7C7C, 0xABABAB, 0xC6C1B6, 0xD9D9D9,
};

/* 10 rows x 10 columns */
static const uint8_t k_grid_common_color[100] = {
    2, 24, 10, 23, 15, 20, 21, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 16, 49, 12, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 13, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
    5, 11, 75, 76, 77, 78, 79, 80, 81, 82,
    14, 83, 6, 17, 7, 84, 85, 86, 87, 88,
    1, 3, 89, 19, 90, 91, 22, 92, 93, 94,
    8, 0, 95, 9, 18, 4, 96, 97, 98, 99,
};
/* 2 rows x 5 columns */
static const uint8_t k_grid_faceline_color[10] = {
    0, 7, 1, 4, 5,
    6, 3, 2, 8, 9,
};
/* 22 rows x 6 columns */
static const uint8_t k_grid_hair[132] = {
    33, 47, 40, 37, 32, 107,
    48, 51, 55, 70, 44, 66,
    52, 50, 38, 49, 43, 31,
    56, 68, 62, 115, 76, 119,
    64, 81, 116, 121, 22, 58,
    60, 87, 125, 117, 73, 75,
    42, 89, 57, 54, 80, 34,
    23, 86, 88, 118, 39, 36,
    45, 67, 59, 65, 41, 30,
    12, 16, 10, 82, 128, 129,
    14, 95, 105, 100, 6, 20,
    93, 102, 27, 4, 17, 110,
    123, 8, 106, 72, 3, 21,
    0, 98, 63, 90, 11, 120,
    5, 74, 108, 94, 124, 25,
    99, 69, 35, 13, 122, 113,
    53, 24, 85, 83, 71, 131,
    96, 101, 29, 7, 15, 112,
    79, 1, 109, 127, 91, 26,
    61, 103, 2, 77, 18, 92,
    84, 9, 19, 130, 97, 104,
    46, 78, 28, 114, 126, 111,
};
/* 10 rows x 6 columns */
static const uint8_t k_grid_eye[60] = {
    2, 4, 0, 8, 39, 17,
    1, 26, 16, 15, 27, 20,
    33, 11, 19, 32, 9, 12,
    23, 34, 21, 25, 40, 35,
    5, 41, 13, 36, 37, 6,
    24, 30, 31, 18, 28, 46,
    7, 44, 38, 42, 45, 29,
    3, 43, 22, 10, 14, 47,
    48, 49, 50, 53, 59, 56,
    54, 58, 57, 55, 51, 52,
};
/* 4 rows x 6 columns */
static const uint8_t k_grid_eyebrow[24] = {
    6, 0, 12, 1, 9, 19,
    7, 21, 8, 17, 5, 4,
    11, 10, 2, 3, 14, 20,
    15, 13, 22, 18, 16, 23,
};
/* 3 rows x 6 columns */
static const uint8_t k_grid_nose[18] = {
    1, 10, 2, 3, 6, 0,
    5, 4, 8, 9, 7, 11,
    13, 14, 12, 17, 16, 15,
};
/* 6 rows x 6 columns */
static const uint8_t k_grid_mouth[36] = {
    23, 1, 18, 21, 22, 5,
    0, 8, 10, 16, 6, 13,
    7, 9, 2, 11, 3, 4,
    15, 17, 20, 19, 14, 12,
    27, 30, 24, 25, 29, 28,
    26, 35, 31, 34, 33, 32,
};
/* 4 rows x 5 columns */
static const uint8_t k_grid_glass[20] = {
    0, 4, 9, 2, 10,
    3, 12, 5, 1, 11,
    13, 8, 14, 7, 6,
    18, 17, 19, 16, 15,
};

uint32_t mii_common_color_rgb(uint8_t c)    { return c < 100 ? k_common_colors[c] : 0; }
uint32_t mii_faceline_color_rgb(uint8_t c)  { return c < 10 ? k_faceline_colors[c] : 0; }

/* the favorite colors (red, orange, yellow, light green, green, blue,
 * light blue, pink, purple, brown, white, black) */
static const uint32_t k_favorite_colors[12] = {
    0xD21E14, 0xFF6E19, 0xFFD820, 0x78D220, 0x00782D, 0x0A48B4,
    0x3CAADE, 0xF55A7D, 0x7328AD, 0x483818, 0xE0E0E0, 0x181814,
};
uint32_t mii_favorite_color_rgb(uint8_t c) { return c < 12 ? k_favorite_colors[c] : 0; }
uint32_t mii_upper_lip_color_rgb(uint8_t c) { return c < 100 ? k_upper_lip_colors[c] : 0; }

#define GRID(arr, cols) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), cols }

static const mii_grid_t k_grids[MII_GRID_COUNT] = {
    [MII_GRID_COMMON_COLOR]   = GRID(k_grid_common_color, 10),
    [MII_GRID_FACELINE_COLOR] = GRID(k_grid_faceline_color, 5),
    [MII_GRID_HAIR]           = GRID(k_grid_hair, 6),
    [MII_GRID_EYE]            = GRID(k_grid_eye, 6),
    [MII_GRID_EYEBROW]        = GRID(k_grid_eyebrow, 6),
    [MII_GRID_NOSE]           = GRID(k_grid_nose, 6),
    [MII_GRID_MOUTH]          = GRID(k_grid_mouth, 6),
    [MII_GRID_GLASS]          = GRID(k_grid_glass, 5),
};

const mii_grid_t *mii_editor_grid(mii_grid_kind_t kind)
{
    return (unsigned)kind < MII_GRID_COUNT ? &k_grids[kind] : NULL;
}

int mii_grid_index_of(const mii_grid_t *grid, uint8_t value)
{
    if (!grid)
        return -1;
    for (int i = 0; i < grid->count; i++)
        if (grid->items[i] == value)
            return i;
    return -1;
}

/* ---- random Miis ---------------------------------------------------------- */

static uint32_t next_random(uint32_t *state)
{
    /* xorshift32 */
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int rnd(uint32_t *state, int n) { return (int)(next_random(state) % (uint32_t)n); }
static int chance(uint32_t *state, int percent) { return rnd(state, 100) < percent; }

void mii_charinfo_init_random(mii_charinfo_t *ci, int gender, uint32_t *state)
{
    if (gender < 0 || gender > 1)
        gender = rnd(state, 2);
    mii_charinfo_init_default(ci, gender);

    ci->favorite_color = (uint8_t)rnd(state, 12);
    ci->faceline_type = (uint8_t)rnd(state, 12);
    ci->faceline_color = (uint8_t)(chance(state, 85) ? rnd(state, 6) : 6 + rnd(state, 4));
    ci->faceline_wrinkle = (uint8_t)(chance(state, 85) ? 0 : 1 + rnd(state, 11));
    ci->faceline_make = (uint8_t)(chance(state, 75) ? 0 : 1 + rnd(state, 11));

    ci->hair_type = (uint8_t)rnd(state, 132);
    ci->hair_flip = (uint8_t)rnd(state, 2);
    ci->hair_color = chance(state, 90) ? mii_ver3_hair_color_to_common((uint8_t)rnd(state, 8)) : (uint8_t)rnd(state, 100);
    ci->eyebrow_color = ci->hair_color;
    ci->beard_color = ci->hair_color;

    ci->eye_type = (uint8_t)rnd(state, 60);
    ci->eye_color = mii_ver3_eye_color_to_common((uint8_t)rnd(state, 6));
    ci->eyebrow_type = (uint8_t)rnd(state, 24);
    ci->nose_type = (uint8_t)rnd(state, 18);
    ci->mouth_type = (uint8_t)rnd(state, 36);
    ci->mouth_color = mii_ver3_mouth_color_to_common((uint8_t)rnd(state, 5));

    if (gender == 0 && chance(state, 25)) {
        ci->mustache_type = (uint8_t)(chance(state, 60) ? 1 + rnd(state, 5) : 0);
        ci->beard_type = (uint8_t)(chance(state, 60) ? 1 + rnd(state, 5) : 0);
    }
    if (chance(state, 20)) {
        ci->glass_type = (uint8_t)(1 + rnd(state, 19));
        ci->glass_color = mii_ver3_glass_color_to_common((uint8_t)rnd(state, 6));
    }
    if (chance(state, 10))
        ci->mole_type = 1;
    /* average body */
    ci->height = 64;
    ci->build = 64;
}
