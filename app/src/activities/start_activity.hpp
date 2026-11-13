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
    StartActivity(OnPicked onPicked, bool lookalike);

    brls::View* createContentView() override;

  private:
    void shuffle();

    OnPicked onPicked;
    bool lookalike;
    std::vector<mii_charinfo_t> choices; // fixed size: previews point into it
    uint32_t seed;
};
