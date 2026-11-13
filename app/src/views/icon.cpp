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

IconView::~IconView()
{
    if (wantsRender)
        HeadRenderer::instance().forget(this);
    if (renderedNvg >= 0)
        nvgDeleteImage(brls::Application::getNVGContext(), renderedNvg);
}

void IconView::setRendered(const HeadRenderer::Spec& spec, float zoom)
{
    rendered = spec;
    renderedZoom = zoom;
    wantsRender = true;
}

void IconView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    const float s = std::fmin(width, height);
    const float ix = x + (width - s) / 2, iy = y + (height - s) / 2;
    if (wantsRender && !requested && HeadRenderer::instance().available()) {
        requested = true;
        HeadRenderer::instance().request(this, rendered, [this](HeadRenderer::Image img, int size, int) {
            renderedImage = img;
            renderedSize = size;
        });
    }
    if (renderedImage && renderedNvg < 0) {
        // a white mask with the part's coverage, like the PNG icons
        std::vector<uint8_t> mask(*renderedImage);
        for (size_t i = 0; i + 3 < mask.size(); i += 4)
            mask[i] = mask[i + 1] = mask[i + 2] = 255;
        renderedNvg = nvgCreateImageRGBA(vg, renderedSize, renderedSize, 0, mask.data());
    }
    if (renderedNvg >= 0) {
        const float z = s * renderedZoom, zx = ix + (s - z) / 2, zy = iy + (s - z) / 2;
        NVGpaint paint = nvgImagePattern(vg, zx, zy, z, z, 0.0f, renderedNvg, 1.0f);
        paint.innerColor = paint.outerColor = color;
        nvgBeginPath(vg);
        nvgRect(vg, ix, iy, s, s);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    } else {
        drawIcon(vg, name, ix, iy, s, color);
    }
    if (!overlay.empty())
        drawIcon(vg, overlay, ix, iy, s, overlayColor);
}
