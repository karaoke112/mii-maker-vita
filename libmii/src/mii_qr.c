/* Mii QR codes: the bytes inside the QR code.
 *
 * 3DS / Wii U (and Tomodachi Life, Miitopia...): 112 bytes, a
 * Ver3StoreData (0x60) sealed with AES-128-CCM:
 *   0x00 u8[8]   bytes 0x0C-0x13 of the Ver3StoreData (the Mii id and the
 *                start of the creator MAC), also the CCM nonce (+ 4 zeros)
 *   0x08 u8[88]  the other 88 bytes, encrypted. (CCM runs over 96 bytes:
 *                those 88 and 8 zeros, whose ciphertext is left out.)
 *   0x60 u8[16]  CCM tag
 * Longer codes (Tomodachi Life) append their own data; it is ignored here.
 *
 * Switch 2: 0x90 bytes: the 112 above, 0x11, then 15 bytes sealed with a
 * second key (same nonce): 0x01, the Switch fields Ver3 cannot hold (face,
 * hair, eye, eyebrow, mouth, beard and glasses color, glasses type, as in
 * an amiibo's StoreData extension), six zeros; then the 16-byte tag.
 *
 * The format is documented by the Mii tools community (Arian Kordi's
 * mii-tomo-dachi-topia-qr-tool, kazuki-4ys' MiiInfoEditorCTR, MiiJS); the
 * code here is our own. The keys are not included (see mii_qr_add_key). */
#include "mii_internal.h"

#include <string.h>

/* ---- AES-128 (encryption only: CCM never needs the inverse) ----------- */

static const uint8_t k_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

typedef struct aes128 {
    uint8_t rk[176]; /* 11 round keys */
} aes128;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static void aes128_init(aes128 *a, const uint8_t key[16])
{
    static const uint8_t rcon[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };
    memcpy(a->rk, key, 16);
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        memcpy(t, a->rk + 4 * (i - 1), 4);
        if (i % 4 == 0) {
            const uint8_t u = t[0];
            t[0] = (uint8_t)(k_sbox[t[1]] ^ rcon[i / 4 - 1]);
            t[1] = k_sbox[t[2]];
            t[2] = k_sbox[t[3]];
            t[3] = k_sbox[u];
        }
        for (int k = 0; k < 4; k++)
            a->rk[4 * i + k] = (uint8_t)(a->rk[4 * (i - 4) + k] ^ t[k]);
    }
}

static void aes128_encrypt(const aes128 *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++)
        s[i] = (uint8_t)(in[i] ^ a->rk[i]);
    for (int round = 1; round <= 10; round++) {
        uint8_t t[16];
        /* SubBytes + ShiftRows (state is column-major: s[4 * col + row]) */
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
                t[4 * c + r] = k_sbox[s[4 * ((c + r) % 4) + r]];
        if (round < 10) { /* MixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *col = t + 4 * c;
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3], all = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
                col[0] = (uint8_t)(a0 ^ all ^ xtime((uint8_t)(a0 ^ a1)));
                col[1] = (uint8_t)(a1 ^ all ^ xtime((uint8_t)(a1 ^ a2)));
                col[2] = (uint8_t)(a2 ^ all ^ xtime((uint8_t)(a2 ^ a3)));
                col[3] = (uint8_t)(a3 ^ all ^ xtime((uint8_t)(a3 ^ a0)));
            }
        }
        for (int i = 0; i < 16; i++)
            s[i] = (uint8_t)(t[i] ^ a->rk[16 * round + i]);
    }
    memcpy(out, s, 16);
}

/* ---- AES-CCM with a 12-byte nonce (L = 3) and a 16-byte tag, no AAD --- */

static void ccm_block(uint8_t flags, const uint8_t nonce[12], uint32_t n, uint8_t out[16])
{
    out[0] = flags;
    memcpy(out + 1, nonce, 12);
    out[13] = (uint8_t)(n >> 16);
    out[14] = (uint8_t)(n >> 8);
    out[15] = (uint8_t)n;
}

/* The tag over `plain` (len bytes). */
static void ccm_tag(const aes128 *a, const uint8_t nonce[12], const uint8_t *plain, size_t len, uint8_t tag[16])
{
    uint8_t x[16], b[16], s0[16];
    ccm_block((uint8_t)(((16 - 2) / 2) << 3 | (3 - 1)), nonce, (uint32_t)len, b);
    aes128_encrypt(a, b, x);
    for (size_t off = 0; off < len; off += 16) {
        for (size_t i = 0; i < 16; i++)
            x[i] ^= off + i < len ? plain[off + i] : 0;
        aes128_encrypt(a, x, x);
    }
    ccm_block(3 - 1, nonce, 0, b);
    aes128_encrypt(a, b, s0);
    for (int i = 0; i < 16; i++)
        tag[i] = (uint8_t)(x[i] ^ s0[i]);
}

/* Counter mode from counter 1: encrypts and decrypts. */
static void ccm_ctr(const aes128 *a, const uint8_t nonce[12], const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t b[16], ks[16];
    for (size_t off = 0; off < len; off += 16) {
        ccm_block(3 - 1, nonce, (uint32_t)(off / 16 + 1), b);
        aes128_encrypt(a, b, ks);
        for (size_t i = 0; i < 16 && off + i < len; i++)
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
    }
}

/* ---- the Mii QR layout ------------------------------------------------ */

/* The two keys are Nintendo's and are not included: the user provides them
 * (mii_qr_add_key). Only a check value of each is kept here, the first 8
 * bytes of AES(key, 16 zero bytes), which tells which key a given one is
 * (and catches typos) without revealing it. */
static const uint8_t k_check_qr[8] = { 0x00, 0x44, 0x14, 0xe1, 0xb6, 0x84, 0x31, 0xcb };
static const uint8_t k_check_switch2[8] = { 0xda, 0x96, 0x73, 0x27, 0xaa, 0x70, 0x9f, 0x85 };

static uint8_t g_qr_key[16], g_switch2_key[16];
static int g_keys;

int mii_qr_add_key(const uint8_t key[16])
{
    if (!key)
        return 0;
    aes128 a;
    aes128_init(&a, key);
    uint8_t zero[16] = { 0 }, check[16];
    aes128_encrypt(&a, zero, check);
    if (memcmp(check, k_check_qr, sizeof(k_check_qr)) == 0) {
        memcpy(g_qr_key, key, 16);
        g_keys |= MII_QR_KEY_3DS;
        return MII_QR_KEY_3DS;
    }
    if (memcmp(check, k_check_switch2, sizeof(k_check_switch2)) == 0) {
        memcpy(g_switch2_key, key, 16);
        g_keys |= MII_QR_KEY_SWITCH2;
        return MII_QR_KEY_SWITCH2;
    }
    return 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Every run of 32 hex digits on a line (labels such as "Switch 2 key:" add
 * stray digits; the check value sorts real keys out): keys may be written as
 * "0123...", "01 23 ...", or "0x01, 0x23, ..." (a C array). */
int mii_qr_add_keys_text(const char *text)
{
    if (!text)
        return 0;
    int found = 0;
    for (const char *line = text; *line;) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        /* the line's hex digits, with "0x" prefixes dropped */
        uint8_t digits[256];
        size_t n = 0;
        for (const char *p = line; p < end && n < sizeof(digits); p++) {
            if (p[0] == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
                p++;
                continue;
            }
            const int d = hex_digit(*p);
            if (d >= 0)
                digits[n++] = (uint8_t)d;
        }
        for (size_t start = 0; start + 32 <= n; start++) {
            uint8_t key[16];
            for (int i = 0; i < 16; i++)
                key[i] = (uint8_t)(digits[start + 2 * i] << 4 | digits[start + 2 * i + 1]);
            found |= mii_qr_add_key(key);
        }
        line = *end ? end + 1 : end;
    }
    return found;
}

int mii_qr_keys(void) { return g_keys; }

#define QR_ID_OFS   0x0C
#define QR_ID_LEN   8
#define QR_BODY_LEN (MII_VER3_STOREDATA_SIZE - QR_ID_LEN) /* 88 */
#define QR_BASE_LEN 112
#define QR_S2_MARK  0x11
#define QR_S2_PLAIN 15

static void nonce_of(const uint8_t id[QR_ID_LEN], uint8_t nonce[12])
{
    memcpy(nonce, id, QR_ID_LEN);
    memset(nonce + QR_ID_LEN, 0, 4);
}

static void seal_base(const uint8_t sd[MII_VER3_STOREDATA_SIZE], uint8_t out[QR_BASE_LEN])
{
    aes128 a;
    aes128_init(&a, g_qr_key);
    uint8_t nonce[12], plain[96], tag[16];
    nonce_of(sd + QR_ID_OFS, nonce);
    memcpy(plain, sd, QR_ID_OFS);
    memcpy(plain + QR_ID_OFS, sd + QR_ID_OFS + QR_ID_LEN, MII_VER3_STOREDATA_SIZE - QR_ID_OFS - QR_ID_LEN);
    memset(plain + QR_BODY_LEN, 0, sizeof(plain) - QR_BODY_LEN);
    ccm_tag(&a, nonce, plain, sizeof(plain), tag);
    memcpy(out, sd + QR_ID_OFS, QR_ID_LEN);
    ccm_ctr(&a, nonce, plain, out + QR_ID_LEN, QR_BODY_LEN);
    memcpy(out + QR_ID_LEN + QR_BODY_LEN, tag, 16);
}

mii_result_t mii_qr_unwrap(const uint8_t *data, size_t size, uint8_t storedata[MII_VER3_STOREDATA_SIZE],
                           uint8_t ext[MII_QR_EXT_SIZE], int *has_ext)
{
    if (!data || !storedata)
        return MII_ERR_ARGUMENT;
    if (has_ext)
        *has_ext = 0;
    if (size < QR_BASE_LEN)
        return MII_ERR_SIZE;
    if (!(g_keys & MII_QR_KEY_3DS))
        return MII_ERR_NO_KEY;
    aes128 a;
    aes128_init(&a, g_qr_key);
    uint8_t nonce[12], body[QR_BODY_LEN];
    nonce_of(data, nonce);
    ccm_ctr(&a, nonce, data + QR_ID_LEN, body, QR_BODY_LEN);
    memcpy(storedata, body, QR_ID_OFS);
    memcpy(storedata + QR_ID_OFS, data, QR_ID_LEN);
    memcpy(storedata + QR_ID_OFS + QR_ID_LEN, body + QR_ID_OFS, QR_BODY_LEN - QR_ID_OFS);
    /* authentic: sealing it again gives the same bytes */
    uint8_t check[QR_BASE_LEN];
    seal_base(storedata, check);
    if (memcmp(check, data, QR_BASE_LEN) != 0)
        return MII_ERR_CRC;

    if (size == MII_QR_PAYLOAD_MAX && data[QR_BASE_LEN] == QR_S2_MARK && (g_keys & MII_QR_KEY_SWITCH2)) {
        aes128 b;
        aes128_init(&b, g_switch2_key);
        uint8_t plain[QR_S2_PLAIN], tag[16];
        const uint8_t *sealed = data + QR_BASE_LEN + 1;
        ccm_ctr(&b, nonce, sealed, plain, QR_S2_PLAIN);
        ccm_tag(&b, nonce, plain, QR_S2_PLAIN, tag);
        int zeros = 1;
        for (int i = 1 + MII_QR_EXT_SIZE; i < QR_S2_PLAIN; i++)
            zeros &= plain[i] == 0;
        if (memcmp(tag, sealed + QR_S2_PLAIN, 16) == 0 && plain[0] == 0x01 && zeros) {
            if (ext)
                memcpy(ext, plain + 1, MII_QR_EXT_SIZE);
            if (has_ext)
                *has_ext = 1;
        }
    }
    return MII_OK;
}

mii_result_t mii_qr_wrap(const uint8_t storedata[MII_VER3_STOREDATA_SIZE], const uint8_t *ext,
                         uint8_t out[MII_QR_PAYLOAD_MAX], size_t *out_size)
{
    if (!storedata || !out)
        return MII_ERR_ARGUMENT;
    if (!(g_keys & MII_QR_KEY_3DS) || (ext && !(g_keys & MII_QR_KEY_SWITCH2)))
        return MII_ERR_NO_KEY;
    seal_base(storedata, out);
    size_t n = QR_BASE_LEN;
    if (ext) {
        aes128 b;
        aes128_init(&b, g_switch2_key);
        uint8_t nonce[12], plain[QR_S2_PLAIN];
        nonce_of(storedata + QR_ID_OFS, nonce);
        memset(plain, 0, sizeof(plain));
        plain[0] = 0x01;
        memcpy(plain + 1, ext, MII_QR_EXT_SIZE);
        out[n++] = QR_S2_MARK;
        ccm_ctr(&b, nonce, plain, out + n, QR_S2_PLAIN);
        ccm_tag(&b, nonce, plain, QR_S2_PLAIN, out + n + QR_S2_PLAIN);
        n += QR_S2_PLAIN + 16;
    }
    if (out_size)
        *out_size = n;
    return MII_OK;
}

mii_result_t mii_qr_encode(const mii_charinfo_t *ci, const mii_ver3_info_t *info, int switch2,
                           uint8_t out[MII_QR_PAYLOAD_MAX], size_t *out_size)
{
    if (!ci || !out)
        return MII_ERR_ARGUMENT;
    uint8_t sd[MII_VER3_STOREDATA_SIZE];
    mii_result_t r = mii_ver3_encode(ci, info, sd);
    if (r != MII_OK)
        return r;
    const uint8_t ext[MII_QR_EXT_SIZE] = {
        ci->faceline_color, ci->hair_color, ci->eye_color, ci->eyebrow_color,
        ci->mouth_color, ci->beard_color, ci->glass_color, ci->glass_type,
    };
    /* without the Switch 2 key: a 3DS / Wii U code */
    return mii_qr_wrap(sd, switch2 && (g_keys & MII_QR_KEY_SWITCH2) ? ext : NULL, out, out_size);
}

mii_result_t mii_qr_decode(mii_charinfo_t *ci, mii_ver3_info_t *info, const uint8_t *data, size_t size,
                           const mii_create_id_t *new_id)
{
    if (!ci)
        return MII_ERR_ARGUMENT;
    uint8_t sd[MII_VER3_STOREDATA_SIZE], ext[MII_QR_EXT_SIZE];
    int has_ext = 0;
    mii_result_t r = mii_qr_unwrap(data, size, sd, ext, &has_ext);
    if (r != MII_OK)
        return r;
    r = mii_ver3_decode(ci, info, sd, sizeof(sd), new_id);
    if (r != MII_OK)
        return r;
    if (has_ext) {
        /* the Switch values the Ver3 palette could not hold */
        mii_charinfo_t full = *ci;
        full.faceline_color = ext[0];
        full.hair_color = ext[1];
        full.eye_color = ext[2];
        full.eyebrow_color = ext[3];
        full.mouth_color = ext[4];
        full.beard_color = ext[5];
        full.glass_color = ext[6];
        full.glass_type = ext[7];
        if (mii_verify(&full, 0) == MII_FIELD_COUNT)
            *ci = full;
    }
    return MII_OK;
}
