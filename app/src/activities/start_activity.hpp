#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "mii_store.hpp"

#include <cstdint>
#include <functional>
#include <vector>

// Where a new Mii starts: "Start from Scratch" (the default male and female
// Miis) or "Choose a Look-Alike" (ten random Miis; Triangle shuffles). The
// pick opens in the editor.
class StartActivity : public brls::Activity
{
  public:
    using OnPicked = std::function<void(const mii_charinfo_t& start)>;
    // `limits`: only Miis that list can keep (the 3DS's, the Wii's)
    StartActivity(OnPicked onPicked, bool lookalike, const MiiStore* limits = nullptr);

    brls::View* createContentView() override;

  private:
    void shuffle();

    OnPicked onPicked;
    bool lookalike;
    const MiiStore* limits;
    std::vector<mii_charinfo_t> choices; // fixed size: previews point into it
    uint32_t seed;
};
