#pragma once

#include <mii/mii.h>

#include <cstdint>
#include <string>
#include <vector>

struct quirc;

// Mii QR codes: making them (the Switch 2 format, which 3DS and Wii U read
// too) and finding them in pictures (camera frames, image files). The bytes
// inside are libmii's mii_qr_* format.
namespace qr {

// Mii QR codes are sealed with Nintendo's keys, which are not included:
// reads them from ux0:data/mii-maker-vita/dat/qrkey.txt (32 hex digits per
// key; the 3DS / Wii U key, and optionally the Switch 2 one). Returns
// mii_qr_keys().
int loadKeys();
std::string keyPath();
// Without the 3DS / Wii U key no codes can be made or read.
inline bool available() { return (mii_qr_keys() & MII_QR_KEY_3DS) != 0; }

// A QR code's modules: size x size, row by row, 1 = dark.
struct Code {
    int size = 0;
    std::vector<uint8_t> modules;
    bool dark(int x, int y) const { return modules[(size_t)y * size + x] != 0; }
};

bool make(const mii_charinfo_t& mii, Code* out);

// A PNG of the code: `scale` pixels per module, a white margin of `border`
// modules.
bool savePng(const Code& code, const std::string& path, int scale = 8, int border = 4);

// Looks for Mii QR codes in an 8-bit grayscale picture; fills `mii` from the
// first one that reads as a Mii. Not thread-safe across calls on the same
// Scanner; use one per thread.
class Scanner
{
  public:
    Scanner();
    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    // `fill` writes the picture into the w x h buffer it is given. quirc
    // misses codes that a slightly different black / white split finds, so
    // besides the picture as it is, versions judged against each pixel's
    // surroundings (uneven light, busy backgrounds) are tried: all of them
    // (variant < 0), or just one (camera frames take turns).
    static constexpr int kVariants = 4;
    template <typename Fill>
    bool scan(int w, int h, Fill fill, mii_charinfo_t* mii, int variant = -1)
    {
        uint8_t* buffer = begin(w, h);
        if (!buffer)
            return false;
        fill(buffer);
        gray.assign(buffer, buffer + (size_t)w * h);
        if (finish(mii))
            return true;
        for (int v = 0; v < kVariants; v++) {
            if (variant >= 0 && v != variant % kVariants)
                continue;
            localThreshold(w, h, v, begin(w, h));
            if (finish(mii))
                return true;
        }
        return false;
    }
    // QR codes were seen but none held a Mii (to tell the user why nothing
    // happens).
    bool sawOtherCode() const { return otherCode; }

  private:
    uint8_t* begin(int w, int h);
    bool finish(mii_charinfo_t* mii);
    void localThreshold(int w, int h, int variant, uint8_t* out);
    ::quirc* q = nullptr;
    std::vector<uint8_t> gray;
    std::vector<uint32_t> integral;
    bool otherCode = false;
};

// Reads a PNG / JPEG / BMP file and looks for a Mii QR code in it.
bool scanImageFile(const std::string& path, mii_charinfo_t* mii);

} // namespace qr
