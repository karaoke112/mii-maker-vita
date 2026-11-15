#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

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
    // `threeDs`: only Miis the 3DS can keep
    StartActivity(OnPicked onPicked, bool lookalike, bool threeDs = false);

    brls::View* createContentView() override;

  private:
    void shuffle();

    OnPicked onPicked;
    bool lookalike;
    bool threeDs;
    std::vector<mii_charinfo_t> choices; // fixed size: previews point into it
    uint32_t seed;
};
