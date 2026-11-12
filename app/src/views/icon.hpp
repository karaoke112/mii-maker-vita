#pragma once

#include <borealis.hpp>

#include "head_renderer.hpp"

#include <string>

// The editor's icons: PNG masks in resources/icons/ built by
// app/tools/make_icons.py from published icon sets (Material Symbols,
// Material Design Icons, Health Icons, Tabler; see
// resources/icons/LICENSES.md). They are white, so any color can be applied.

// Draws icon `name` (e.g. "face") in the square (x, y, size), in `color`.
void drawIcon(NVGcontext* vg, const std::string& name, float x, float y, float size, NVGcolor color);

// A view showing one icon, centered, optionally with a second icon drawn
// over it (e.g. a filled star with its outline).
class IconView : public brls::View
{
  public:
    IconView(std::string name, float size, NVGcolor color);
    ~IconView() override;
    void setColor(NVGcolor c) { color = c; }
    // Shows a rendered Mii part instead (as a mask in the icon's color)
    // once the face resource has drawn it; the named icon until then, or
    // without a resource.
    void setRendered(const HeadRenderer::Spec& spec, float zoom = 1.0f);
    void setOverlay(std::string name, NVGcolor c)
    {
        overlay = std::move(name);
        overlayColor = c;
    }
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    HeadRenderer::Spec rendered;
    bool wantsRender = false, requested = false;
    HeadRenderer::Image renderedImage;
    int renderedSize = 0;
    int renderedNvg = -1;
    float renderedZoom = 1.0f;
    std::string name;
    NVGcolor color;
    std::string overlay;
    NVGcolor overlayColor;
};
