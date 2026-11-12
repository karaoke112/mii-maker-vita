#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>

// "Send to Another Vita" / "Receive from Another Vita": joins the ad-hoc
// network, then lists the Vitas that are receiving (sending) or waits for
// offers and asks before saving each Mii (receiving). See trade.hpp.
class TradeActivity : public brls::Activity
{
  public:
    // receiving; `onSaved` runs after a received Mii was saved
    explicit TradeActivity(std::function<void()> onSaved);
    // sending `mii`
    explicit TradeActivity(const mii_charinfo_t& mii);

    brls::View* createContentView() override;

  private:
    bool receiving;
    mii_charinfo_t mii {};
    std::function<void()> onSaved;
};
