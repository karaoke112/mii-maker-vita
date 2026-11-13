#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>
#include <vector>

// "Send to Another Vita" / "Receive from Another Vita": joins the ad-hoc
// network, then lists the Vitas that are receiving (sending) or waits for
// offers and asks before saving each Mii (receiving). See trade.hpp.
class TradeActivity : public brls::Activity
{
  public:
    // receiving; `onSaved` runs after a received Mii was saved
    explicit TradeActivity(std::function<void()> onSaved);
    // sending `miis`, offered one after another to the receiver chosen
    explicit TradeActivity(std::vector<mii_charinfo_t> miis);

    brls::View* createContentView() override;

  private:
    bool receiving;
    std::vector<mii_charinfo_t> miis;
    mii_charinfo_t shown {}; // the Mii being offered (the preview points here)
    brls::Label* count = nullptr;
    std::function<void()> onSaved;
};
