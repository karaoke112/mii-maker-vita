#include "ui_theme.hpp"

namespace ui {

void applyTheme()
{
    brls::Theme& t = brls::Theme::getLightTheme();
    t.addColor("brls/clear", kBackground);
    t.addColor("brls/background", kBackground);
    t.addColor("brls/text", kText);
    t.addColor("brls/accent", kAccent);
    t.addColor("brls/click_pulse", nvgRGBA(10, 100, 220, 40));
    // a solid blue focus frame instead of the animated two-tone one
    t.addColor("brls/highlight/color1", kAccent);
    t.addColor("brls/highlight/color2", kAccent);
    t.addColor("brls/highlight/background", kTile);
    t.addColor("brls/applet_frame/separator", kRule);
    t.addColor("brls/button/primary_enabled_background", kAccent);
    t.addColor("brls/button/highlight_enabled_text", kAccent);
    t.addColor("brls/list/listItem_value_color", kAccent);
    t.addColor("brls/slider/line_filled", kAccent);
    t.addColor("brls/slider/line_empty", nvgRGB(200, 200, 200));
    t.addColor("brls/slider/pointer_border_color", nvgRGB(190, 190, 190));

    brls::Style s = brls::getStyle();
    s.addMetric("brls/highlight/stroke_width", 4.0f);
    s.addMetric("brls/highlight/shadow_opacity", 0.0f);
    s.addMetric("brls/highlight/corner_radius", 4.0f);
}

} // namespace ui
