#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "head_renderer.hpp"

// Live preview of a Mii: the head rendered in 3D by HeadRenderer (on a
// worker thread) from the face resource the user installed. Without that
// file it shows a short note saying where to put it.
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
