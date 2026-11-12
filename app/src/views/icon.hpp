#pragma once

#include <borealis.hpp>

#include <string>

// The editor's icons: PNG masks in resources/icons/ built by
// app/tools/make_icons.py from published icon sets (Material Symbols,
// Material Design Icons, game-icons.net, Tabler; see
// resources/icons/LICENSES.md). They are white, so any color can be applied.

// Draws icon `name` (e.g. "face") in the square (x, y, size), in `color`.
void drawIcon(NVGcontext* vg, const std::string& name, float x, float y, float size, NVGcolor color);

// A view showing one icon, centered, optionally with a second icon drawn
// over it (e.g. a filled star with its outline).
class IconView : public brls::View
{
  public:
    IconView(std::string name, float size, NVGcolor color);
    void setColor(NVGcolor c) { color = c; }
    void setOverlay(std::string name, NVGcolor c)
    {
        overlay = std::move(name);
        overlayColor = c;
    }
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    std::string name;
    NVGcolor color;
    std::string overlay;
    NVGcolor overlayColor;
};
