#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>

// "Create QR Code": the Mii's QR code on screen (Switch 2 format: 3DS and
// Wii U read it too), also saved as a picture in ux0:data/mii/qr/.
class QrShowActivity : public brls::Activity
{
  public:
    explicit QrShowActivity(const mii_charinfo_t& mii)
        : mii(mii)
    {
    }
    brls::View* createContentView() override;

  private:
    mii_charinfo_t mii;
};

// "Scan QR Code": the back camera looks for a Mii QR code; the first one
// found is handed to `onFound` and the screen closes.
class QrScanActivity : public brls::Activity
{
  public:
    using OnFound = std::function<void(const mii_charinfo_t&)>;
    explicit QrScanActivity(OnFound onFound)
        : onFound(std::move(onFound))
    {
    }
    brls::View* createContentView() override;

  private:
    OnFound onFound;
};
