#include "views/mii_preview.hpp"

#include <cmath>
#include <cstring>

using namespace brls::literals;

MiiPreview::MiiPreview(const mii_charinfo_t* mii, int renderSize)
    : mii(mii)
    , renderSize(renderSize)
{
    this->setFocusable(false);
    std::memset(&requested, 0, sizeof(requested));
}

MiiPreview::~MiiPreview()
{
    HeadRenderer::instance().forget(this);
    if (nvgImage >= 0)
        nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
}

void MiiPreview::request3D()
{
    HeadRenderer& hr = HeadRenderer::instance();
    if (!hr.available())
        return;
    // whole steps, so turning reuses cached renders
    const float yaw = std::round(yawRef() / 5.0f) * 5.0f;
    if (hasRequested && requestedYaw == yaw && std::memcmp(&requested, mii, sizeof(requested)) == 0)
        return;
    requested = *mii;
    requestedYaw = yaw;
    hasRequested = true;
    HeadRenderer::Spec spec;
    spec.mii = *mii;
    spec.size = renderSize;
    spec.yaw = yaw;
    if (stage) {
        spec.zoom = 0.8f; // room for long hair
        spec.centerY = -4.0f;
    }
    hr.request(this, spec, [this](HeadRenderer::Image img, int) {
        image = img;
        imageDirty = true;
    });
}

void MiiPreview::pollStick()
{
    const brls::Time now = brls::getCPUTimeUsec();
    const float dt = lastPoll ? std::fmin(0.1f, (float)(now - lastPoll) / 1e6f) : 0.0f;
    lastPoll = now;
    const float ax = brls::Application::getControllerState().axes[brls::RIGHT_X];
    if (std::fabs(ax) > 0.3f)
        yawRef() = std::fmax(-70.0f, std::fmin(70.0f, yawRef() + ax * 120.0f * dt));
}

void MiiPreview::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx)
{
    const float cx = x + width / 2;
    const float nameHeight = stage ? 0.0f : 40.0f;

    if (stage)
        pollStick();

    // Backdrop
    if (!stage) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, 18);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + height, nvgRGB(250, 250, 252), nvgRGB(222, 226, 234)));
    nvgFill(vg);
    }

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));

    if (!HeadRenderer::instance().available()) {
        // No face resource installed: say so rather than drawing a guess.
        nvgFillColor(vg, nvgRGB(120, 120, 135));
        const bool large = width > 220;
        nvgFontSize(vg, large ? 20.0f : 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgTextBox(vg, x + 12, y + (height - nameHeight) * (large ? 0.35f : 0.42f), width - 24,
            (large ? "preview/no_resource"_i18n : "preview/no_preview"_i18n).c_str(), nullptr);
    } else {
        request3D();
        if (image) {
            if (nvgImage < 0 || nvgImageSize != renderSize) {
                if (nvgImage >= 0)
                    nvgDeleteImage(vg, nvgImage);
                nvgImage = nvgCreateImageRGBA(vg, renderSize, renderSize, 0, image->data());
                nvgImageSize = renderSize;
            } else if (imageDirty) {
                nvgUpdateImage(vg, nvgImage, image->data());
            }
            imageDirty = false;
        }
        if (nvgImage >= 0) {
            // the head fills the middle ~60% of the rendered square
            const float side = stage ? std::fmin(width, height) * 0.95f
                                     : std::fmin(width * 1.15f, height - nameHeight);
            const float ix = cx - side / 2, iy = y + (height - nameHeight - side) / 2 + (stage ? -12 : 6);
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, side, side);
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, side, side, 0.0f, nvgImage, 1.0f));
            nvgFill(vg);
        }
    }

    if (stage)
        return;

    // Name
    char name[64];
    mii_name_get_utf8(mii, name, sizeof(name));
    nvgFontSize(vg, 22);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGB(60, 60, 70));
    nvgText(vg, cx, y + height - 12, name, nullptr);
}
