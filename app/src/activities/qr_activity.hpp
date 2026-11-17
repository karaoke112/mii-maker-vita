#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "mii_store.hpp"

#include <functional>

// "Create QR Code": the Mii's QR code on screen (Switch 2 format: 3DS and
// Wii U read it too), also saved as a picture in ux0:data/mii/qr/.
class QrShowActivity : public brls::Activity
{
  public:
    // `store`: the Mii's list (its profile goes into the code)
    QrShowActivity(const mii_charinfo_t& mii, MiiStore* store)
        : mii(mii)
        , store(store)
    {
    }
    brls::View* createContentView() override;

  private:
    mii_charinfo_t mii;
    MiiStore* store;
};

// "Scan QR Code": the back camera looks for a Mii QR code; the first one
// found is handed to `onFound` and the screen closes.
class QrScanActivity : public brls::Activity
{
  public:
    // the Mii, and what its code carries besides the face (the profile)
    using OnFound = std::function<void(const mii_charinfo_t&, const mii_ver3_info_t&)>;
    explicit QrScanActivity(OnFound onFound)
        : onFound(std::move(onFound))
    {
    }
    brls::View* createContentView() override;

  private:
    OnFound onFound;
};
