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

    // Stages take touches (to turn the Mii) though they are not focusable.
    brls::View* hitTest(brls::Point point) override
    {
        if (stage && interactive && this->getVisibility() == brls::Visibility::VISIBLE &&
            this->getFrame().pointInside(point))
            return this;
        return brls::View::hitTest(point);
    }

        void setYaw(float degrees) { yawRef() = degrees; }
    float getYaw() { return yawRef(); }
    // Shares the turn angle with other previews (the editor's pages).
    void setYawSource(float* source) { yawSource = source; }
    // Stage mode: no card backdrop or name, the head drawn large, and the
    // right stick turns it.
    void setStage(bool on) { stage = on; }
    // Whether a stage turns with the right stick and by dragging (default).
    void setInteractive(bool on) { interactive = on; }
    // How much of the Mii a stage shows once FFLBodyRes.dat is installed
    // (until then, the head).
    enum class Framing { BUST, FULL_BODY };
    void setFraming(Framing f) { framing = f; }
    // SELECT in the editor shows render times on the stage.
    inline static bool showTimings = false;

    // The plain color behind a stage (default: the page background).
    void setBackdrop(NVGcolor c)
    {
        backdrop = c;
        hasBackdrop = true;
    }

    // Extra magnification on top of the framing (1 = none).
    void setZoom(float z) { extraZoom = z; }

  private:
    void request3D();
    float& yawRef() { return yawSource ? *yawSource : yaw; }
    void pollStick();

    const mii_charinfo_t* mii;
    int renderSize;
    float yaw = 0.0f;
    float* yawSource = nullptr;
    bool stage = false;
    bool interactive = true;
    float extraZoom = 1.0f;
    Framing framing = Framing::BUST;
    bool requestedBust = false;
    brls::Time lastPoll = 0;

    // what we last asked HeadRenderer for
    mii_charinfo_t requested;
    float requestedYaw = 0.0f;
    bool hasRequested = false;

    // newest finished render, and the NanoVG image showing it
    HeadRenderer::Image image;
    bool imageDirty = false;
    int nvgImage = -1;
    int nvgImageSize = 0;
    int imageSize = 0; // side of `image` (a quick pass is smaller)
    int gpuImage = -1; // a GPU render (owned by GpuRenderer)
    NVGcolor backdrop {};
    bool hasBackdrop = false;
};

// Keeps an angle in [-180, 180).
float wrapYaw(float degrees);
