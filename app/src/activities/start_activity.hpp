#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>

// "Start from Scratch": pick the style (base) of a new Mii from two tiles
// showing the default male and female Miis.
class StartActivity : public brls::Activity
{
  public:
    using OnPicked = std::function<void(int gender)>;
    explicit StartActivity(OnPicked onPicked);

    brls::View* createContentView() override;

  private:
    OnPicked onPicked;
    mii_charinfo_t bases[2];
};
