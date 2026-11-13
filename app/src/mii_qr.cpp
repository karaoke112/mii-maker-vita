#include "mii_qr.hpp"

#include "mii_library.hpp"
#include "paths.hpp"
#include "settings.hpp"

#include <borealis/core/logger.hpp>

#include <qrcodegen.h>
#include <quirc.h>
#include <stb_image.h> // implemented by borealis' NanoVG

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image_write.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace qr {

std::string keyPath()
{
    return paths::datDir() + "/qrkey.txt";
}

int loadKeys()
{
    FILE* f = std::fopen(keyPath().c_str(), "rb");
    if (!f) {
        brls::Logger::info("qr: no key file at {}", keyPath());
        return mii_qr_keys();
    }
    std::string text;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 16384)
        text.append(buf, n);
    std::fclose(f);
    const int found = mii_qr_add_keys_text(text.c_str());
    brls::Logger::info("qr: keys from {}: 3DS {}, Switch 2 {}", keyPath(), (found & MII_QR_KEY_3DS) ? "yes" : "no",
        (found & MII_QR_KEY_SWITCH2) ? "yes" : "no");
    return mii_qr_keys();
}

bool make(const mii_charinfo_t& mii, Code* out)
{
    uint8_t payload[MII_QR_PAYLOAD_MAX];
    size_t size = 0;
    mii_ver3_info_t info;
    MiiLibrary::instance().ver3Info(mii, &info);
    if (mii_qr_encode(&mii, &info, Settings::get().qrSwitch2 ? 1 : 0, payload, &size) != MII_OK)
        return false;
    std::vector<uint8_t> data(qrcodegen_BUFFER_LEN_MAX), temp(qrcodegen_BUFFER_LEN_MAX);
    std::memcpy(data.data(), payload, size);
    if (!qrcodegen_encodeBinary(data.data(), size, temp.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
            qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) // the QR code ends up in `temp`
        return false;
    const int n = qrcodegen_getSize(temp.data());
    out->size = n;
    out->modules.assign((size_t)n * n, 0);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            out->modules[(size_t)y * n + x] = qrcodegen_getModule(temp.data(), x, y) ? 1 : 0;
    return true;
}

bool savePng(const Code& code, const std::string& path, int scale, int border)
{
    const int side = (code.size + 2 * border) * scale;
    std::vector<uint8_t> px((size_t)side * side, 255);
    for (int y = 0; y < code.size; y++)
        for (int x = 0; x < code.size; x++)
            if (code.dark(x, y))
                for (int j = 0; j < scale; j++)
                    std::memset(&px[(size_t)((y + border) * scale + j) * side + (x + border) * scale], 0, scale);
    return stbi_write_png(path.c_str(), side, side, 1, px.data(), side) != 0;
}

Scanner::Scanner()
    : q(quirc_new())
{
}

Scanner::~Scanner()
{
    if (q)
        quirc_destroy(q);
}

uint8_t* Scanner::begin(int w, int h)
{
    if (!q)
        return nullptr;
    int cw = 0, ch = 0;
    quirc_begin(q, &cw, &ch);
    if (cw != w || ch != h) {
        if (quirc_resize(q, w, h) < 0)
            return nullptr;
    }
    return quirc_begin(q, nullptr, nullptr);
}

// Black where a pixel is darker than the mean around it, white elsewhere;
// the variants differ in box size and margin.
void Scanner::localThreshold(int w, int h, int variant, uint8_t* out)
{
    static const struct {
        int div, margin;
    } kinds[kVariants] = { { 16, 15 }, { 8, 6 }, { 32, 6 }, { 16, 0 } };
    const int div = kinds[variant].div, margin = kinds[variant].margin;
    const int W = w + 1;
    integral.assign((size_t)W * (h + 1), 0);
    for (int y = 0; y < h; y++) {
        uint32_t row = 0;
        for (int x = 0; x < w; x++) {
            row += gray[(size_t)y * w + x];
            integral[(size_t)(y + 1) * W + x + 1] = integral[(size_t)y * W + x + 1] + row;
        }
    }
    const int r = std::max(8, std::min(w, h) / div);
    for (int y = 0; y < h; y++) {
        const int y0 = std::max(0, y - r), y1 = std::min(h, y + r + 1);
        for (int x = 0; x < w; x++) {
            const int x0 = std::max(0, x - r), x1 = std::min(w, x + r + 1);
            const uint32_t sum = integral[(size_t)y1 * W + x1] - integral[(size_t)y0 * W + x1] -
                integral[(size_t)y1 * W + x0] + integral[(size_t)y0 * W + x0];
            const int mean = (int)(sum / (uint32_t)((x1 - x0) * (y1 - y0)));
            out[(size_t)y * w + x] = gray[(size_t)y * w + x] + margin < mean ? 0 : 255;
        }
    }
}

bool Scanner::finish(mii_charinfo_t* mii)
{
    quirc_end(q);
    otherCode = false;
    const int count = quirc_count(q);
    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err == QUIRC_ERROR_DATA_ECC) {
            // seen from behind (a mirrored picture): try it flipped
            quirc_flip(&code);
            err = quirc_decode(&code, &data);
        }
        if (err != QUIRC_SUCCESS)
            continue;
        mii_charinfo_t ci;
        if (mii_qr_decode(&ci, nullptr, data.payload, (size_t)data.payload_len, nullptr) == MII_OK &&
            mii_verify(&ci, 0) == MII_FIELD_COUNT) {
            *mii = ci;
            return true;
        }
        otherCode = true;
    }
    return false;
}

bool scanImageFile(const std::string& path, mii_charinfo_t* mii)
{
    int w = 0, h = 0, n = 0;
    uint8_t* gray = stbi_load(path.c_str(), &w, &h, &n, 1);
    if (!gray)
        return false;
    Scanner s;
    const bool found = s.scan(w, h, [&](uint8_t* buf) { std::memcpy(buf, gray, (size_t)w * h); }, mii);
    stbi_image_free(gray);
    return found;
}

} // namespace qr
