#include "views/mii_preview.hpp"

#include "gpu_renderer.hpp"
#include "ui_theme.hpp"

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
            if (stage && interactive && status.state != brls::GestureState::UNSURE) {
                if (frontSource)
                    *frontSource = false;
                yawRef() = wrapYaw(yawRef() - status.delta.x * 0.6f); // the Mii follows the finger
            }
        },
        brls::PanAxis::HORIZONTAL));
}

void MiiPreview::setShareKey(const void* key)
{
    HeadRenderer::instance().forget(this);
    shareKey = key;
    shareCount[key]++;
    shareFrames[key].leader = this;
}

MiiPreview::~MiiPreview()
{
    if (shareKey) {
        if (--shareCount[shareKey] == 0) {
            shareCount.erase(shareKey);
            shareEpochs.erase(shareKey);
            shareImages.erase(shareKey);
            shareFrames.erase(shareKey);
            HeadRenderer::instance().forget(shareKey);
        } else {
            // the screen below takes the framing back
            auto f = shareFrames.find(shareKey);
            if (f != shareFrames.end() && f->second.leader == this)
                f->second.leader = nullptr;
            // the others keep the picture; they ask again when shown
            HeadRenderer::instance().invalidate(shareKey);
            shareEpochs[shareKey]++;
        }
    } else {
        HeadRenderer::instance().forget(this);
    }
    if (nvgImage >= 0)
        nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
}

void MiiPreview::request3D()
{
    HeadRenderer& hr = HeadRenderer::instance();
    if (!hr.available())
        return;
    // whole steps, so turning reuses cached renders; the GPU draws fast
    // enough for fine steps (smooth turning and tilting)
    const bool fine = hr.gpuActive() && stage;
    const float yawStep = fine ? 1.0f : 5.0f, pitchStep = fine ? 0.5f : 2.5f;
    const float yaw = std::round(yawRef() / yawStep) * yawStep;
    const float pitch = std::round(pitchRef() / pitchStep) * pitchStep;
    const Frame& fr = frameRef();
    const float pivot = std::round(fr.pivot * 4.0f) / 4.0f, zoom = std::round(fr.zoom * 400.0f) / 400.0f;
    const bool sharedChanged = shareKey && shareEpochs[shareKey] != shareEpoch;
    if (hasRequested && !sharedChanged && requestedYaw == yaw && requestedPitch == pitch &&
        (!stage || !hr.hasBody() || (requestedPivot == pivot && requestedZoom == zoom)) &&
        std::memcmp(&requested, mii, sizeof(requested)) == 0)
        return;
    requestedPivot = pivot;
    requestedZoom = zoom;
    if (shareKey)
        shareEpoch = ++shareEpochs[shareKey];
    requested = *mii;
    requestedYaw = yaw;
    requestedPitch = pitch;
    hasRequested = true;
    HeadRenderer::Spec spec;
    spec.mii = *mii;
    // the GPU draws stages at about the size they are shown (sharp, and
    // its multisampling smooths the edges); the CPU keeps to renderSize
    spec.size = stage && hr.gpuActive() ? (compact ? 256 : 512) : renderSize;
    spec.yaw = yaw;
    spec.pitch = pitch;
    // thumbnails (not stages) are drawn once and kept on the memory card,
    // nearest the cursor first
    spec.lowPriority = !stage;
    spec.smooth = !stage;
    requestedBust = false;
    if (stage && hr.hasBody()) {
        // the eased framing (see stageTarget); centerY is relative to the
        // full-body camera height of 92
        spec.fullBody = true;
        spec.zoom = zoom;
        spec.centerY = pivot - 92.0f;
        requestedBust = framing == Framing::BUST;
    } else if (stage) {
        spec.zoom = 0.8f; // room for long hair
        spec.centerY = -4.0f;
    }
    spec.zoom *= extraZoom;
    if (stage) {
        // stages sit on a plain color: the GPU can draw them on it
        const NVGcolor bg = hasBackdrop ? backdrop : ui::background();
        spec.background = 0xFF000000u | ((uint32_t)(bg.r * 255.0f + 0.5f) << 16) |
            ((uint32_t)(bg.g * 255.0f + 0.5f) << 8) | (uint32_t)(bg.b * 255.0f + 0.5f);
    }
    hr.request(owner(), spec, [this, zoom](HeadRenderer::Image img, int size, int gpu) {
        imageSize = size;
        imageZoom = zoom;
        if (gpu > 0) {
            gpuImage = gpu;
            image.reset();
        } else {
            gpuImage = -1;
            image = img;
            imageDirty = true;
            if (shareKey)
                shareImages[shareKey] = { img, size };
        }
    });
}

// Where the stage camera looks and how close: the bust centers on the head
// (which sits higher on a taller Mii: the neck is at 93.9 x the height
// scale, as in libmii3d); the whole body keeps one framing, so height shows.
void MiiPreview::stageTarget(float* pivot, float* zoom) const
{
    if (compact) {
        *pivot = framing == Framing::BUST ? 104.0f : 90.0f;
        *zoom = framing == Framing::BUST ? 1.8f : 1.25f;
    } else if (framing == Framing::BUST) {
        const float heightScale = mii->height * 0.006015625f + 0.5f;
        *pivot = 10.7766f * 8.715f * heightScale + 26.0f;
        *zoom = 1.5f;
    } else {
        *pivot = 90.0f;
        *zoom = 1.1f;
    }
}

void MiiPreview::easeFrame(float dt)
{
    float pivot, zoom;
    stageTarget(&pivot, &zoom);
    Frame& f = frameRef();
    // with several screens of one Mii drawn (a transition), only the
    // newest moves the shared framing
    if (shareKey) {
        if (f.leader && f.leader != this)
            return;
        f.leader = this;
    }
    if (!f.set) {
        f.pivot = pivot;
        f.zoom = zoom;
        f.set = true;
        return;
    }
    // about a fifth of a second to settle, every frame drawn on the way
    const float k = std::fmin(1.0f, dt * 12.0f);
    f.pivot += (pivot - f.pivot) * k;
    f.zoom += (zoom - f.zoom) * k;
    if (std::fabs(pivot - f.pivot) < 0.05f && std::fabs(zoom - f.zoom) < 0.002f) {
        f.pivot = pivot;
        f.zoom = zoom;
    }
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
    const brls::ControllerState& state = brls::Application::getControllerState();
    const float ax = state.axes[brls::RIGHT_X], ay = state.axes[brls::RIGHT_Y];
    // a dead zone for resting sticks, then up to half a turn per second;
    // no limit: the Mii turns all the way round
    const float dead = 0.25f;
    if (frontSource && *frontSource) {
        if (std::fabs(ax) > dead || std::fabs(ay) > dead) {
            *frontSource = false; // the stick takes over
        } else {
            // ease back: fast when far, slowing near the front
            auto approach = [dt](float v) {
                const float step = std::fmax(40.0f, std::fabs(v) * 6.0f) * dt;
                return std::fabs(v) <= step ? 0.0f : v - (v > 0 ? step : -step);
            };
            yawRef() = approach(wrapYaw(yawRef()));
            pitchRef() = approach(pitchRef());
            if (yawRef() == 0.0f && pitchRef() == 0.0f)
                *frontSource = false;
            return;
        }
    }
    if (std::fabs(ax) > dead) {
        const float amount = (std::fabs(ax) - dead) / (1.0f - dead);
        yawRef() = wrapYaw(yawRef() + (ax > 0 ? 1.0f : -1.0f) * amount * 180.0f * dt);
    }
    // up / down tilts it a little (stick down: the Mii looks down)
    if (std::fabs(ay) > dead) {
        const float amount = (std::fabs(ay) - dead) / (1.0f - dead);
        const float p = pitchRef() + (ay > 0 ? 1.0f : -1.0f) * amount * 60.0f * dt;
        pitchRef() = std::fmax(-kMaxPitch, std::fmin(kMaxPitch, p));
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
    const bool dark = ui::isDark(ui::theme());
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + height, dark ? nvgRGB(78, 78, 82) : nvgRGB(250, 250, 252),
                         dark ? nvgRGB(58, 58, 62) : nvgRGB(222, 226, 234)));
    nvgFill(vg);
    }

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));

    if (!HeadRenderer::instance().available()) {
        // No face resource installed: say so rather than drawing a guess.
        nvgFillColor(vg, ui::textDim());
        const bool large = width > 220;
        nvgFontSize(vg, large ? 20.0f : 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgTextBox(vg, x + 12, y + (height - nameHeight) * (large ? 0.35f : 0.42f), width - 24,
            (large ? "preview/no_resource"_i18n : "preview/no_preview"_i18n).c_str(), nullptr);
    } else {
        if (stage && HeadRenderer::instance().hasBody()) {
            const brls::Time now = brls::getCPUTimeUsec();
            // a new screen's first frame: one frame's step, not a jump
            easeFrame(lastEase ? std::fmin(0.1f, (float)(now - lastEase) / 1e6f) : 1.0f / 60.0f);
            lastEase = now;
        }
        request3D();
        // a shared render may have been redrawn (or remade) by another screen
        if (shareKey && gpuImage > 0) {
            const int latest = GpuRenderer::instance().imageOf(shareKey);
            if (latest > 0)
                gpuImage = latest;
        } else if (shareKey && gpuImage <= 0 && !image) {
            // a new screen: the latest picture
            gpuImage = GpuRenderer::instance().imageOf(shareKey);
            auto it = shareImages.find(shareKey);
            if (gpuImage <= 0 && it != shareImages.end() && it->second.first) {
                image = it->second.first;
                imageSize = it->second.second;
                imageDirty = true;
            }
        }
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
        const int shown = gpuImage > 0 ? gpuImage : nvgImage;
        if (shown >= 0) {
            float side, iy;
            bool clipped = false;
            const float ix0 = cx;
            if (stage && compact) {
                const bool bust = framing == Framing::BUST;
                side = bust ? std::fmin(width * 1.12f, height) : std::fmin(width, height) * 0.95f;
                iy = bust ? y + height - side : y + (height - side) / 2 - 12;
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, width, height);
            } else if (stage && HeadRenderer::instance().hasBody()) {
                // between the whole body (t = 0) and the bust (t = 1), as the
                // framing eases: the bust is close up with the head in the
                // middle of the stage, like the Switch 2
                // placed by the zoom the picture was drawn with, which can be
                // a frame behind the eased framing
                const Frame& f = frameRef();
                const float shownZoom = imageZoom > 0.0f ? imageZoom : f.zoom;
                const float t = std::fmax(0.0f, std::fmin(1.0f, (shownZoom - 1.1f) / (1.5f - 1.1f)));
                const float fullSide = std::fmin(width, height) * 0.95f, bustSide = height * 1.05f;
                side = fullSide + (bustSide - fullSide) * t;
                iy = y + (height - side) / 2 - 12.0f * (1.0f - t);
                // a soft light behind the head (a shadow on the light theme)
                const bool dark = ui::theme() != ui::Theme::LIGHT;
                const float gx = cx, gy = iy + side * (0.5f - 0.18f * (1.0f - t)), gr = side * (0.22f + 0.06f * t);
                // the stage and its shadow stay above the button bar
                nvgSave(vg);
                nvgIntersectScissor(vg, x - width, y - height, width * 3, height * 2);
                clipped = true;
                nvgBeginPath(vg);
                nvgRect(vg, x - width, y - height, width * 3, height * 3);
                nvgFillPaint(vg, nvgRadialGradient(vg, gx, gy, gr * 0.3f, gr * 1.15f,
                                     dark ? nvgRGBA(255, 255, 255, 70) : nvgRGBA(0, 0, 0, 34),
                                     dark ? nvgRGBA(255, 255, 255, 0) : nvgRGBA(0, 0, 0, 0)));
                nvgFill(vg);
            } else {
                // the head fills the middle ~60% of the rendered square
                side = stage ? std::fmin(width, height) * 0.95f : std::fmin(width * 1.15f, height - nameHeight);
                iy = y + (height - nameHeight - side) / 2 + (stage ? -12 : 6);
            }
            const float ix = ix0 - side / 2;
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, side, side);
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, side, side, 0.0f, shown, 1.0f));
            nvgFill(vg);
            if ((stage && compact) || clipped)
                nvgRestore(vg);
        }
    }

    if (stage) {
        if (showTimings) {
            nvgFontSize(vg, 16);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, ui::textDim());
            nvgText(vg, x + 8, y + 8, HeadRenderer::instance().timings().c_str(), nullptr);
        }
        return;
    }

    // Name
    char name[64];
    mii_name_get_utf8(mii, name, sizeof(name));
    nvgFontSize(vg, 22);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, ui::text());
    nvgText(vg, cx, y + height - 12, name, nullptr);
}
