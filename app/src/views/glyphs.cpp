#include "views/glyphs.hpp"

#include "ui_theme.hpp"

#include <cmath>

namespace {

struct Pen {
    NVGcontext* vg;
    float x, y, s;
    float X(float u) const { return x + u * s; }
    float Y(float v) const { return y + v * s; }
    float L(float d) const { return d * s; }

    void circle(float u, float v, float r) const
    {
        nvgBeginPath(vg);
        nvgCircle(vg, X(u), Y(v), L(r));
    }
    void ellipse(float u, float v, float rx, float ry) const
    {
        nvgBeginPath(vg);
        nvgEllipse(vg, X(u), Y(v), L(rx), L(ry));
    }
    void line(float u0, float v0, float u1, float v1) const
    {
        nvgBeginPath(vg);
        nvgMoveTo(vg, X(u0), Y(v0));
        nvgLineTo(vg, X(u1), Y(v1));
    }
    void stroke(NVGcolor c, float w) const
    {
        nvgStrokeColor(vg, c);
        nvgStrokeWidth(vg, L(w));
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    void fill(NVGcolor c) const
    {
        nvgFillColor(vg, c);
        nvgFill(vg);
    }
};

// A standing figure: head, torso, arms, legs. `sx`/`sy` stretch the body
// (for the build/height pictograms); `skirt` draws a flared torso.
void figure(const Pen& p, NVGcolor c, float cx, float sx, float sy, bool skirt)
{
    const float top = 0.5f - 0.42f * sy;
    const float head = 0.10f;
    const float headY = top + head;
    p.circle(cx, headY, head);
    p.fill(c);

    const float neck = headY + head + 0.03f * sy;
    const float waist = neck + 0.34f * sy;
    const float feet = 0.5f + 0.42f * sy;
    const float half = 0.085f * sx;

    nvgBeginPath(p.vg);
    if (skirt) {
        nvgMoveTo(p.vg, p.X(cx - half), p.Y(neck));
        nvgLineTo(p.vg, p.X(cx + half), p.Y(neck));
        nvgLineTo(p.vg, p.X(cx + half * 1.9f), p.Y(waist + 0.02f));
        nvgLineTo(p.vg, p.X(cx - half * 1.9f), p.Y(waist + 0.02f));
        nvgClosePath(p.vg);
    } else {
        nvgRoundedRect(p.vg, p.X(cx - half), p.Y(neck), p.L(half * 2), p.L(waist - neck), p.L(0.05f));
    }
    p.fill(c);

    // arms
    const float arm = 0.05f;
    p.line(cx - half, neck + 0.03f, cx - half - 0.09f * sx, waist - 0.04f);
    p.stroke(c, arm);
    p.line(cx + half, neck + 0.03f, cx + half + 0.09f * sx, waist - 0.04f);
    p.stroke(c, arm);
    // legs
    const float leg = 0.055f * std::fmax(1.0f, sx * 0.9f);
    const float gap = std::fmax(0.035f, half * 0.45f);
    p.line(cx - gap, waist, cx - gap, feet - leg / 2);
    p.stroke(c, leg);
    p.line(cx + gap, waist, cx + gap, feet - leg / 2);
    p.stroke(c, leg);
}

// A two-headed arrow from (u0, v0) to (u1, v1).
void arrow(const Pen& p, NVGcolor c, float u0, float v0, float u1, float v1)
{
    p.line(u0, v0, u1, v1);
    p.stroke(c, 0.035f);
    const float dx = u1 - u0, dy = v1 - v0;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float ux = dx / len, uy = dy / len;
    for (int end = 0; end < 2; end++) {
        const float tx = end ? u1 : u0, ty = end ? v1 : v0;
        const float dir = end ? 1.0f : -1.0f;
        const float h = 0.07f;
        nvgBeginPath(p.vg);
        nvgMoveTo(p.vg, p.X(tx + dir * ux * h * 0.6f), p.Y(ty + dir * uy * h * 0.6f));
        nvgLineTo(p.vg, p.X(tx - dir * ux * h * 0.5f - uy * h), p.Y(ty - dir * uy * h * 0.5f + ux * h));
        nvgLineTo(p.vg, p.X(tx - dir * ux * h * 0.5f + uy * h), p.Y(ty - dir * uy * h * 0.5f - ux * h));
        nvgClosePath(p.vg);
        p.fill(c);
    }
}

} // namespace

void drawGlyph(NVGcontext* vg, Glyph glyph, float x, float y, float size, NVGcolor c, NVGcolor fillColor)
{
    nvgSave(vg);
    const Pen p { vg, x, y, size };
    const float w = 0.075f; // standard line weight

    switch (glyph) {
    case Glyph::FACE:
        p.ellipse(0.5f, 0.5f, 0.34f, 0.40f);
        p.stroke(c, w);
        p.circle(0.38f, 0.46f, 0.045f);
        p.fill(c);
        p.circle(0.62f, 0.46f, 0.045f);
        p.fill(c);
        nvgBeginPath(vg);
        nvgArc(vg, p.X(0.5f), p.Y(0.56f), p.L(0.15f), 0.35f, NVG_PI - 0.35f, NVG_CW);
        p.stroke(c, w * 0.8f);
        break;

    case Glyph::HAIR:
        p.circle(0.5f, 0.52f, 0.36f);
        p.stroke(c, w);
        // the hair: a cap over the top with a swept fringe
        nvgBeginPath(vg);
        nvgArc(vg, p.X(0.5f), p.Y(0.52f), p.L(0.36f), NVG_PI, 0.0f, NVG_CW);
        nvgBezierTo(vg, p.X(0.80f), p.Y(0.62f), p.X(0.66f), p.Y(0.40f), p.X(0.52f), p.Y(0.42f));
        nvgBezierTo(vg, p.X(0.38f), p.Y(0.44f), p.X(0.24f), p.Y(0.56f), p.X(0.14f), p.Y(0.52f));
        nvgClosePath(vg);
        p.fill(c);
        break;

    case Glyph::EYEBROWS:
        p.line(0.10f, 0.44f, 0.40f, 0.56f);
        p.stroke(c, w * 1.2f);
        p.line(0.60f, 0.56f, 0.90f, 0.44f);
        p.stroke(c, w * 1.2f);
        break;

    case Glyph::EYES:
        for (int side = 0; side < 2; side++) {
            const float cx = side ? 0.70f : 0.30f;
            p.ellipse(cx, 0.5f, 0.17f, 0.13f);
            p.stroke(c, w);
            p.circle(cx + 0.02f, 0.5f, 0.07f);
            p.fill(c);
        }
        break;

    case Glyph::NOSE:
        nvgBeginPath(vg);
        nvgMoveTo(vg, p.X(0.56f), p.Y(0.18f));
        nvgBezierTo(vg, p.X(0.50f), p.Y(0.40f), p.X(0.36f), p.Y(0.58f), p.X(0.40f), p.Y(0.72f));
        nvgBezierTo(vg, p.X(0.43f), p.Y(0.82f), p.X(0.56f), p.Y(0.80f), p.X(0.64f), p.Y(0.76f));
        p.stroke(c, w * 1.1f);
        break;

    case Glyph::MOUTH:
        nvgBeginPath(vg);
        nvgMoveTo(vg, p.X(0.14f), p.Y(0.5f));
        nvgBezierTo(vg, p.X(0.30f), p.Y(0.26f), p.X(0.44f), p.Y(0.34f), p.X(0.5f), p.Y(0.38f));
        nvgBezierTo(vg, p.X(0.56f), p.Y(0.34f), p.X(0.70f), p.Y(0.26f), p.X(0.86f), p.Y(0.5f));
        nvgBezierTo(vg, p.X(0.74f), p.Y(0.74f), p.X(0.26f), p.Y(0.74f), p.X(0.14f), p.Y(0.5f));
        nvgClosePath(vg);
        p.fill(c);
        p.line(0.16f, 0.5f, 0.84f, 0.5f);
        p.stroke(ui::kBackground, w * 0.6f);
        break;

    case Glyph::FACIAL_HAIR:
        p.circle(0.5f, 0.5f, 0.38f);
        p.stroke(c, w);
        nvgBeginPath(vg);
        nvgMoveTo(vg, p.X(0.5f), p.Y(0.60f));
        nvgBezierTo(vg, p.X(0.40f), p.Y(0.54f), p.X(0.28f), p.Y(0.58f), p.X(0.26f), p.Y(0.70f));
        nvgBezierTo(vg, p.X(0.36f), p.Y(0.66f), p.X(0.44f), p.Y(0.70f), p.X(0.5f), p.Y(0.68f));
        nvgBezierTo(vg, p.X(0.56f), p.Y(0.70f), p.X(0.64f), p.Y(0.66f), p.X(0.74f), p.Y(0.70f));
        nvgBezierTo(vg, p.X(0.72f), p.Y(0.58f), p.X(0.60f), p.Y(0.54f), p.X(0.5f), p.Y(0.60f));
        nvgClosePath(vg);
        p.fill(c);
        break;

    case Glyph::MOLE:
        p.circle(0.5f, 0.5f, 0.38f);
        p.stroke(c, w);
        p.circle(0.60f, 0.60f, 0.05f);
        p.fill(c);
        break;

    case Glyph::STYLE:
        figure(p, c, 0.5f, 1.0f, 1.0f, true);
        break;

    case Glyph::BODY:
        figure(p, c, 0.5f, 1.0f, 1.0f, false);
        break;

    case Glyph::GLASSES:
        p.circle(0.27f, 0.5f, 0.17f);
        p.stroke(c, w);
        p.circle(0.73f, 0.5f, 0.17f);
        p.stroke(c, w);
        nvgBeginPath(vg);
        nvgArc(vg, p.X(0.5f), p.Y(0.52f), p.L(0.07f), NVG_PI + 0.3f, -0.3f, NVG_CW);
        p.stroke(c, w * 0.8f);
        break;

    case Glyph::STAR: {
        nvgBeginPath(vg);
        for (int i = 0; i < 10; i++) {
            const float r = (i % 2) ? 0.19f : 0.42f;
            const float a = -NVG_PI / 2 + i * NVG_PI / 5;
            const float u = 0.5f + r * std::cos(a), v = 0.54f + r * std::sin(a);
            if (i == 0)
                nvgMoveTo(vg, p.X(u), p.Y(v));
            else
                nvgLineTo(vg, p.X(u), p.Y(v));
        }
        nvgClosePath(vg);
        p.fill(fillColor);
        p.stroke(c, w * 0.7f);
        break;
    }

    case Glyph::PERSON_A:
        figure(p, c, 0.5f, 1.0f, 1.0f, false);
        break;
    case Glyph::PERSON_B:
        figure(p, c, 0.5f, 1.0f, 1.0f, true);
        break;

    case Glyph::BODY_SHORT:
        figure(p, c, 0.38f, 1.0f, 0.62f, false);
        arrow(p, ui::kAccent, 0.80f, 0.30f, 0.80f, 0.70f);
        break;
    case Glyph::BODY_TALL:
        figure(p, c, 0.38f, 1.0f, 1.08f, false);
        arrow(p, ui::kAccent, 0.80f, 0.12f, 0.80f, 0.88f);
        break;
    case Glyph::BODY_THIN:
        figure(p, c, 0.5f, 0.55f, 1.0f, false);
        arrow(p, ui::kAccent, 0.22f, 0.5f, 0.36f, 0.5f);
        arrow(p, ui::kAccent, 0.64f, 0.5f, 0.78f, 0.5f);
        break;
    case Glyph::BODY_WIDE:
        figure(p, c, 0.5f, 1.9f, 1.0f, false);
        arrow(p, ui::kAccent, 0.02f, 0.5f, 0.16f, 0.5f);
        arrow(p, ui::kAccent, 0.84f, 0.5f, 0.98f, 0.5f);
        break;
    }
    nvgRestore(vg);
}

GlyphView::GlyphView(Glyph glyph, float size)
    : glyph(glyph)
    , color(ui::kText)
    , fill(ui::kText)
{
    this->setDimensions(size, size);
    this->setFocusable(false);
}

void GlyphView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    const float s = std::fmin(width, height);
    drawGlyph(vg, glyph, x + (width - s) / 2, y + (height - s) / 2, s, color, fill);
}
