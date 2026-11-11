#include "views/mii_preview.hpp"

#include <mii/mii_editor.h>

#include <cmath>

static NVGcolor rgb(uint32_t c, float alpha = 1.0f)
{
    return nvgRGBAf(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, alpha);
}

// Approximate favorite colors for the shirt; only a UI hint.
static const uint32_t kFavorite[12] = {
    0xD21E14, 0xFF6E19, 0xFFD820, 0x78D220, 0x00782D, 0x0A48B4,
    0x3CAADE, 0xF55A7D, 0x7328AD, 0x483818, 0xE0E0E0, 0x181814,
};

MiiPreview::MiiPreview(const mii_charinfo_t* mii)
    : mii(mii)
{
    this->setFocusable(false);
}

// Feature positions are expressed relative to the default Mii (eye_y 12,
// eyebrow_y 10, nose_y 9, mouth_y 13), one field step = `u`.
void MiiPreview::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx)
{
    const mii_charinfo_t& m = *mii;
    const float cx = x + width / 2;

    // Backdrop
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, 18);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + height, nvgRGB(250, 250, 252), nvgRGB(222, 226, 234)));
    nvgFill(vg);

    const float r = std::fmin(width * 0.30f, height * 0.17f);
    const float hy = y + height * 0.33f;
    const float jaw = 1.04f + (m.faceline_type % 4) * 0.04f; // a hint of face shape
    const float u = r * 0.055f;

    // Body: height 0-127 and build 0-127 stretch it.
    const float bodyTop = hy + r * jaw * 0.95f;
    const float bodyH = (y + height - 40 - bodyTop) * (0.70f + 0.30f * m.height / 127.0f);
    const float bodyW = r * (1.15f + 0.75f * m.build / 127.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx - bodyW / 2, bodyTop, bodyW, bodyH, bodyW * 0.28f);
    nvgFillColor(vg, rgb(kFavorite[m.favorite_color % 12]));
    nvgFill(vg);

    // Head
    nvgBeginPath(vg);
    nvgEllipse(vg, cx, hy, r, r * jaw);
    nvgFillColor(vg, rgb(mii_faceline_color_rgb(m.faceline_color)));
    nvgFill(vg);

    // Hair: a cap down to the hairline, side-parted by hair_flip.
    {
        const float vol = 1.04f + (m.hair_type % 7) * 0.025f;
        const float hairline = hy - r * (0.42f - (m.hair_type % 5) * 0.03f);
        nvgSave(vg);
        nvgScissor(vg, x, y, width, hairline - y);
        nvgBeginPath(vg);
        nvgEllipse(vg, cx, hy - r * 0.04f, r * vol, r * jaw * vol);
        nvgFillColor(vg, rgb(mii_common_color_rgb(m.hair_color)));
        nvgFill(vg);
        nvgRestore(vg);
        // the part
        const float px = cx + (m.hair_flip ? -r * 0.35f : r * 0.35f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, px, hairline);
        nvgLineTo(vg, px + (m.hair_flip ? r * 0.12f : -r * 0.12f), hy - r * jaw * 0.9f);
        nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 40));
        nvgStrokeWidth(vg, u * 0.6f);
        nvgStroke(vg);
    }

    // Eyebrows
    for (int side = -1; side <= 1; side += 2) {
        float ex = cx + side * (r * 0.26f + m.eyebrow_x * u * 0.8f);
        float ey = hy - r * 0.30f + (m.eyebrow_y - 10) * u;
        float len = r * (0.20f + m.eyebrow_scale * 0.02f);
        float thick = u * (0.8f + m.eyebrow_aspect * 0.15f);
        nvgSave(vg);
        nvgTranslate(vg, ex, ey);
        nvgRotate(vg, (m.eyebrow_rotate - 6) * 0.06f * side);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, -len / 2, -thick / 2, len, thick, thick / 2);
        nvgFillColor(vg, rgb(mii_common_color_rgb(m.eyebrow_color)));
        nvgFill(vg);
        nvgRestore(vg);
    }

    // Eyes
    const float eyeY = hy - r * 0.10f + (m.eye_y - 12) * u;
    const float eyeDX = r * 0.24f + m.eye_x * u * 0.8f;
    for (int side = -1; side <= 1; side += 2) {
        float ew = r * (0.07f + m.eye_scale * 0.010f);
        float eh = ew * (0.85f + m.eye_aspect * 0.10f);
        nvgSave(vg);
        nvgTranslate(vg, cx + side * eyeDX, eyeY);
        nvgRotate(vg, (m.eye_rotate - 4) * 0.07f * -side);
        nvgBeginPath(vg);
        nvgEllipse(vg, 0, 0, ew, eh);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgEllipse(vg, 0, eh * 0.05f, ew * 0.62f, eh * 0.78f);
        nvgFillColor(vg, rgb(mii_common_color_rgb(m.eye_color)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, -ew * 0.2f, -eh * 0.25f, ew * 0.18f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 220));
        nvgFill(vg);
        nvgRestore(vg);
    }

    // Glasses
    if (m.glass_type > 0) {
        float gy = eyeY + (m.glass_y - 10) * u;
        float gw = r * (0.36f + m.glass_scale * 0.025f);
        for (int side = -1; side <= 1; side += 2) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx + side * eyeDX - gw / 2, gy - gw * 0.36f, gw, gw * 0.72f,
                gw * 0.12f * (m.glass_type % 3 + 1));
            nvgStrokeColor(vg, rgb(mii_common_color_rgb(m.glass_color)));
            nvgStrokeWidth(vg, u * 0.7f);
            nvgStroke(vg);
        }
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - eyeDX + gw / 2, gy);
        nvgLineTo(vg, cx + eyeDX - gw / 2, gy);
        nvgStroke(vg);
    }

    // Nose
    {
        float ny = hy + r * 0.14f + (m.nose_y - 9) * u;
        float ns = r * (0.06f + m.nose_scale * 0.008f);
        nvgBeginPath(vg);
        nvgArc(vg, cx, ny - ns * 0.4f, ns, 0.3f, NVG_PI - 0.3f, NVG_CW);
        nvgStrokeColor(vg, nvgRGBA(120, 60, 40, 110));
        nvgStrokeWidth(vg, u * 0.6f);
        nvgStroke(vg);
    }

    // Beard and mustache
    const NVGcolor beard = rgb(mii_common_color_rgb(m.beard_color));
    const float mouthY = hy + r * 0.44f + (m.mouth_y - 13) * u;
    if (m.beard_type > 0) {
        nvgSave(vg);
        nvgScissor(vg, x, hy + r * 0.20f, width, height);
        nvgBeginPath(vg);
        nvgEllipse(vg, cx, hy, r * 1.01f, r * jaw * 1.03f);
        nvgPathWinding(vg, NVG_SOLID);
        nvgEllipse(vg, cx, mouthY, r * 0.30f, r * 0.14f);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillColor(vg, beard);
        nvgFill(vg);
        nvgRestore(vg);
    }
    if (m.mustache_type > 0) {
        float ms = r * (0.16f + m.mustache_scale * 0.02f);
        float mty = mouthY - u * 2.4f + (m.mustache_y - 10) * u * 0.5f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx - ms, mty - u * 1.1f, ms * 2, u * 2.2f, u * 1.1f);
        nvgFillColor(vg, beard);
        nvgFill(vg);
    }

    // Mouth: a smile, wider with scale, fuller with aspect.
    {
        float mw = r * (0.15f + m.mouth_scale * 0.02f);
        nvgBeginPath(vg);
        nvgArc(vg, cx, mouthY - mw * 0.55f, mw, 0.55f, NVG_PI - 0.55f, NVG_CW);
        nvgStrokeColor(vg, rgb(mii_common_color_rgb(m.mouth_color)));
        nvgStrokeWidth(vg, u * (0.8f + m.mouth_aspect * 0.18f));
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    // Mole
    if (m.mole_type) {
        float mx = cx + (m.mole_x - 8) * u * 1.4f;
        float moy = hy - r * 0.55f + m.mole_y * u * 1.1f;
        nvgBeginPath(vg);
        nvgCircle(vg, mx, moy, u * (0.45f + m.mole_scale * 0.09f));
        nvgFillColor(vg, nvgRGBA(40, 25, 20, 220));
        nvgFill(vg);
    }

    // Name
    char name[64];
    mii_name_get_utf8(&m, name, sizeof(name));
    nvgFontSize(vg, 22);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGB(60, 60, 70));
    nvgText(vg, cx, y + height - 12, name, nullptr);
}
