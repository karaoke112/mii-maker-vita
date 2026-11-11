#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "head_renderer.hpp"

// Live preview of a Mii.
//
// With the user's Wii U face resource installed, the head is rendered in 3D
// (HeadRenderer, on a worker thread) and drawn over a simple body. Without
// it, or while the first 3D image is still being made, a 2D stand-in drawn
// with NanoVG is shown instead; it reflects every field too.
class MiiPreview : public brls::View
{
  public:
    explicit MiiPreview(const mii_charinfo_t* mii, int renderSize = 256);
    ~MiiPreview() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

    void setYaw(float degrees) { yaw = degrees; }
    float getYaw() const { return yaw; }

  private:
    void drawFallbackHead(NVGcontext* vg, float cx, float hy, float r, float jaw);
    void request3D();

    const mii_charinfo_t* mii;
    int renderSize;
    float yaw = 0.0f;

    // what we last asked HeadRenderer for
    mii_charinfo_t requested;
    float requestedYaw = 0.0f;
    bool hasRequested = false;

    // newest finished render, and the NanoVG image showing it
    HeadRenderer::Image image;
    bool imageDirty = false;
    int nvgImage = -1;
    int nvgImageSize = 0;
};
