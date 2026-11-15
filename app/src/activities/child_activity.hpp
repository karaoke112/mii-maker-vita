#pragma once

#include "child_generator.hpp"

#include <borealis.hpp>
#include <mii/mii.h>

#include "mii_store.hpp"

#include <array>
#include <functional>

// The Child Generator's result: the child of two Miis at six stages of life,
// side by side (or, Kid-o-matic, one Mii at those stages). Each opens in the editor (Done saves it to the Mii list and
// comes back here, as often as wanted); Triangle makes a new child; Circle
// asks, then goes back to the Mii list.
class ChildActivity : public brls::Activity
{
  public:
    // saved to `store` (the app's list or the 3DS's)
    ChildActivity(const mii_charinfo_t& parentA, const mii_charinfo_t& parentB, MiiStore* store, std::function<void()> onClosed);
    // Kid-o-matic: `mii` growing up
    ChildActivity(const mii_charinfo_t& mii, MiiStore* store, std::function<void()> onClosed);

    brls::View* createContentView() override;

  private:
    void generate();
    void refreshLabels();

    mii_charinfo_t parentA, parentB;
    bool kid = false; // Kid-o-matic
    MiiStore* store;
    std::function<void()> onClosed;
    std::array<mii_charinfo_t, child::kStages> stages; // the previews point into it
    std::array<bool, child::kStages> saved {};
    std::array<brls::Label*, child::kStages> names {};
    std::array<brls::Label*, child::kStages> marks {};
};
