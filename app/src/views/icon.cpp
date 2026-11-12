#include "views/icon.hpp"

#include <borealis/core/assets.hpp>

#include <cmath>
#include <map>

// NanoVG images, loaded once per icon (the editor keeps one NanoVG context).
static int iconImage(NVGcontext* vg, const std::string& name)
{
    static std::map<std::string, int> images;
    auto it = images.find(name);
    if (it != images.end())
        return it->second;
    const std::string path = std::string(BRLS_RESOURCES) + "icons/" + name + ".png";
    const int image = nvgCreateImage(vg, path.c_str(), 0);
    if (image <= 0)
        brls::Logger::error("missing icon {}", path);
    images[name] = image;
    return image;
}

void drawIcon(NVGcontext* vg, const std::string& name, float x, float y, float size, NVGcolor color)
{
    const int image = iconImage(vg, name);
    if (image <= 0)
        return;
    NVGpaint paint = nvgImagePattern(vg, x, y, size, size, 0.0f, image, 1.0f);
    // the icon is a white mask: the paint color tints it
    paint.innerColor = paint.outerColor = color;
    nvgBeginPath(vg);
    nvgRect(vg, x, y, size, size);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

IconView::IconView(std::string name, float size, NVGcolor color)
    : name(std::move(name))
    , color(color)
    , overlayColor(color)
{
    this->setDimensions(size, size);
    this->setFocusable(false);
}

void IconView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    const float s = std::fmin(width, height);
    const float ix = x + (width - s) / 2, iy = y + (height - s) / 2;
    drawIcon(vg, name, ix, iy, s, color);
    if (!overlay.empty())
        drawIcon(vg, overlay, ix, iy, s, overlayColor);
}
