#include "ui_theme.hpp"

namespace ui {

namespace {

struct Palette {
    NVGcolor background, tile, text, textDim, rule, ring, accent, sliderEmpty;
};

const Palette kLight = {
    nvgRGB(235, 235, 235), nvgRGB(247, 247, 247), nvgRGB(30, 30, 30), nvgRGB(150, 150, 150),
    nvgRGB(196, 196, 196), nvgRGB(190, 190, 190), nvgRGB(10, 100, 220), nvgRGB(205, 205, 205),
};
const Palette kDark = {
    nvgRGB(40, 40, 40), nvgRGB(70, 70, 70), nvgRGB(238, 238, 238), nvgRGB(140, 140, 140),
    nvgRGB(85, 85, 85), nvgRGB(110, 110, 110), nvgRGB(30, 215, 140), nvgRGB(90, 90, 90),
};

Theme current = Theme::LIGHT;
const Palette& pal() { return current == Theme::DARK ? kDark : kLight; }

void fill(brls::Theme& t, const Palette& p, bool dark)
{
    t.addColor("brls/clear", p.background);
    t.addColor("brls/background", p.background);
    t.addColor("brls/text", p.text);
    t.addColor("brls/accent", p.accent);
    t.addColor("brls/click_pulse", nvgRGBA((int)(p.accent.r * 255), (int)(p.accent.g * 255), (int)(p.accent.b * 255), 40));
    // a solid focus frame in the accent color instead of the animated two-tone one
    t.addColor("brls/highlight/color1", p.accent);
    t.addColor("brls/highlight/color2", p.accent);
    t.addColor("brls/highlight/background", p.tile);
    t.addColor("brls/applet_frame/separator", p.rule);
    t.addColor("brls/sidebar/separator", p.rule);
    t.addColor("brls/header/border", p.rule);
    t.addColor("brls/button/primary_enabled_background", p.accent);
    t.addColor("brls/button/primary_enabled_text", dark ? nvgRGB(20, 20, 20) : nvgRGB(255, 255, 255));
    t.addColor("brls/button/default_enabled_background", p.tile);
    t.addColor("brls/button/default_enabled_text", p.text);
    t.addColor("brls/button/highlight_enabled_text", p.accent);
    t.addColor("brls/button/enabled_border_color", p.rule);
    t.addColor("brls/list/listItem_value_color", p.accent);
    t.addColor("brls/slider/line_filled", p.accent);
    t.addColor("brls/slider/line_empty", p.sliderEmpty);
}

} // namespace

void applyTheme(Theme theme)
{
    current = theme;
    fill(brls::Theme::getLightTheme(), kLight, false);
    fill(brls::Theme::getDarkTheme(), kDark, true);

    brls::Style s = brls::getStyle();
    s.addMetric("brls/highlight/stroke_width", 4.0f);
    s.addMetric("brls/highlight/shadow_opacity", 0.0f);
    s.addMetric("brls/highlight/corner_radius", 4.0f);
}

void applyVariant()
{
    brls::Application::getPlatform()->setThemeVariant(
        current == Theme::DARK ? brls::ThemeVariant::DARK : brls::ThemeVariant::LIGHT);
}

Theme theme() { return current; }

NVGcolor background() { return pal().background; }
NVGcolor tile() { return pal().tile; }
NVGcolor text() { return pal().text; }
NVGcolor textDim() { return pal().textDim; }
NVGcolor rule() { return pal().rule; }
NVGcolor ring() { return pal().ring; }
NVGcolor accent() { return pal().accent; }

} // namespace ui
