#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

// Live preview of the Mii being edited.
//
// This is a deliberately simple 2D stand-in drawn with NanoVG so the editor
// is usable before the 3D renderer exists (docs/ARCHITECTURE.md, "Renderer").
// It reflects every positional/scale/color field so edits are visible, but
// it does not try to reproduce the real part artwork.
class MiiPreview : public brls::View
{
  public:
    explicit MiiPreview(const mii_charinfo_t* mii);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    const mii_charinfo_t* mii;
};
