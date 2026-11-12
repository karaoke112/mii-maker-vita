#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "head_renderer.hpp"

#include <map>

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
    // Shares the turn and tilt angles with other previews (the editor's
    // pages).
    void setYawSource(float* source) { yawSource = source; }
    void setPitchSource(float* source) { pitchSource = source; }
    // While *source is true the Mii turns smoothly back to face the front
    // (then it is set to false).
    void setFrontSource(bool* source) { frontSource = source; }
    // Previews with the same share key (the editor's screens) use one
    // render: a new screen shows the latest picture at once instead of
    // nothing, and a screen shown again redraws if another one rendered.
    void setShareKey(const void* key);
    // How far the right stick tilts the Mii up or down.
    static constexpr float kMaxPitch = 15.0f;
    // Stage mode: no card backdrop or name, the head drawn large, and the
    // right stick turns it.
    void setStage(bool on) { stage = on; }
    // Whether a stage turns with the right stick and by dragging (default).
    void setInteractive(bool on) { interactive = on; }
    // How much of the Mii a stage shows once FFLBodyRes.dat is installed
    // (until then, the head).
    enum class Framing { BUST, FULL_BODY };
    void setFraming(Framing f) { framing = f; }
    // Small stages (the look-alike tiles): the plain bust framing, kept
    // inside the view, no glow.
    void setCompact(bool on) { compact = on; }
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
    const void* owner() const { return shareKey ? shareKey : this; }
    const void* shareKey = nullptr;
    uint64_t shareEpoch = 0;
    inline static std::map<const void*, int> shareCount;
    inline static std::map<const void*, uint64_t> shareEpochs;
    inline static std::map<const void*, std::pair<HeadRenderer::Image, int>> shareImages; // CPU renders
    // the stage's framing, eased toward its target (shared between screens,
    // so going to and from the Body page glides)
    struct Frame {
        float pivot = 0, zoom = 0; // camera height (model units), zoom
        bool set = false;
        // the screen whose framing it follows: the newest one (the others
        // are on their way out, or hidden behind it)
        const MiiPreview* leader = nullptr;
    };
    Frame frame;
    inline static std::map<const void*, Frame> shareFrames;
    Frame& frameRef() { return shareKey ? shareFrames[shareKey] : frame; }
    void stageTarget(float* pivot, float* zoom) const;
    void easeFrame(float dt);
    float& yawRef() { return yawSource ? *yawSource : yaw; }
    float& pitchRef() { return pitchSource ? *pitchSource : pitch; }
    void pollStick();

    const mii_charinfo_t* mii;
    int renderSize;
    float yaw = 0.0f;
    float* yawSource = nullptr;
    float pitch = 0.0f;
    float* pitchSource = nullptr;
    bool* frontSource = nullptr;
    bool stage = false;
    bool compact = false;
    bool interactive = true;
    float extraZoom = 1.0f;
    Framing framing = Framing::BUST;
    bool requestedBust = false;
    brls::Time lastPoll = 0;

    // what we last asked HeadRenderer for
    mii_charinfo_t requested;
    float requestedYaw = 0.0f;
    float requestedPitch = 0.0f;
    float requestedPivot = 0.0f, requestedZoom = 0.0f;
    brls::Time lastEase = 0;
    bool hasRequested = false;

    // newest finished render, and the NanoVG image showing it
    HeadRenderer::Image image;
    bool imageDirty = false;
    int nvgImage = -1;
    int nvgImageSize = 0;
    int imageSize = 0; // side of `image` (a quick pass is smaller)
    float imageZoom = 0.0f; // the framing zoom the shown picture was drawn with
    int gpuImage = -1; // a GPU render (owned by GpuRenderer)
    NVGcolor backdrop {};
    bool hasBackdrop = false;
};

// Keeps an angle in [-180, 180).
float wrapYaw(float degrees);
