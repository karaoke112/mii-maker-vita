#pragma once

#include <borealis.hpp>

// Simple line pictograms for the editor (category icons, style figures,
// body sliders). All original vector drawings made with NanoVG, so nothing
// is taken from Nintendo's artwork.
enum class Glyph {
    FACE,
    HAIR,
    EYEBROWS,
    EYES,
    NOSE,
    MOUTH,
    FACIAL_HAIR,
    MOLE,
    STYLE,
    BODY,
    GLASSES,
    STAR,
    PERSON_A, // trousers
    PERSON_B, // skirt
    BODY_SHORT,
    BODY_TALL,
    BODY_THIN,
    BODY_WIDE,
};

// Draws `glyph` centered in the square (x, y, size). `color` is the line
// color; `fill` is used by glyphs that have a fill (the star).
void drawGlyph(NVGcontext* vg, Glyph glyph, float x, float y, float size, NVGcolor color, NVGcolor fill);

// A view that shows one glyph, centered.
class GlyphView : public brls::View
{
  public:
    GlyphView(Glyph glyph, float size);
    void setGlyphColor(NVGcolor c) { color = c; }
    void setFillColor(NVGcolor c) { fill = c; }
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    Glyph glyph;
    NVGcolor color;
    NVGcolor fill;
};
