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
    // stages turn by dragging across the Mii
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) {
            if (stage && interactive && status.state != brls::GestureState::UNSURE)
                yawRef() = wrapYaw(yawRef() + status.delta.x * 0.6f);
        },
        brls::PanAxis::HORIZONTAL));
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
    requestedBust = false;
    if (stage && hr.hasBody()) {
        spec.fullBody = true;
        if (framing == Framing::BUST) { // head and chest, like the Switch editor
            spec.zoom = 1.6f;
            spec.centerY = 12.0f;
            requestedBust = true;
        } else {
            spec.zoom = 1.1f;
            spec.centerY = -2.0f;
        }
    } else if (stage) {
        spec.zoom = 0.8f; // room for long hair
        spec.centerY = -4.0f;
    }
    spec.zoom *= extraZoom;
    hr.request(this, spec, [this](HeadRenderer::Image img, int size) {
        image = img;
        imageSize = size;
        imageDirty = true;
    });
}

float wrapYaw(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees >= 180.0f)
        degrees -= 360.0f;
    if (degrees < -180.0f)
        degrees += 360.0f;
    return degrees;
}

void MiiPreview::pollStick()
{
    const brls::Time now = brls::getCPUTimeUsec();
    const float dt = lastPoll ? std::fmin(0.1f, (float)(now - lastPoll) / 1e6f) : 0.0f;
    lastPoll = now;
    const float ax = brls::Application::getControllerState().axes[brls::RIGHT_X];
    // a dead zone for resting sticks, then up to half a turn per second;
    // no limit: the Mii turns all the way round
    const float dead = 0.25f;
    if (std::fabs(ax) > dead) {
        const float amount = (std::fabs(ax) - dead) / (1.0f - dead);
        yawRef() = wrapYaw(yawRef() + (ax > 0 ? 1.0f : -1.0f) * amount * 180.0f * dt);
    }
}

void MiiPreview::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx)
{
    const float cx = x + width / 2;
    const float nameHeight = stage ? 0.0f : 40.0f;

    if (stage && interactive)
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
            if (nvgImage < 0 || nvgImageSize != imageSize) {
                if (nvgImage >= 0)
                    nvgDeleteImage(vg, nvgImage);
                nvgImage = nvgCreateImageRGBA(vg, imageSize, imageSize, 0, image->data());
                nvgImageSize = imageSize;
            } else if (imageDirty) {
                nvgUpdateImage(vg, nvgImage, image->data());
            }
            imageDirty = false;
        }
        if (nvgImage >= 0) {
            // the head fills the middle ~60% of the rendered square
            const bool bust = stage && requestedBust;
            const float side = bust ? std::fmin(width * 1.12f, height)
                : stage           ? std::fmin(width, height) * 0.95f
                                  : std::fmin(width * 1.15f, height - nameHeight);
            const float ix = cx - side / 2;
            // a bust is cut off at the bottom, so it stands on the panel's edge
            const float iy = bust ? y + height - side
                                  : y + (height - nameHeight - side) / 2 + (stage ? -12 : 6);
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, side, side);
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, side, side, 0.0f, nvgImage, 1.0f));
            nvgFill(vg);
        }
    }

    if (stage) {
        if (showTimings) {
            nvgFontSize(vg, 16);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGB(120, 120, 120));
            nvgText(vg, x + 8, y + 8, HeadRenderer::instance().timings().c_str(), nullptr);
        }
        return;
    }

    // Name
    char name[64];
    mii_name_get_utf8(mii, name, sizeof(name));
    nvgFontSize(vg, 22);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGB(60, 60, 70));
    nvgText(vg, cx, y + height - 12, name, nullptr);
}
