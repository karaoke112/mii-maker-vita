#pragma once

#include "ui_theme.hpp"

// App settings, kept in ux0:data/mii-maker-vita/settings.txt as key=value
// lines (unknown keys are ignored, missing ones keep their defaults).
struct Settings {
    ui::Theme theme = ui::Theme::LIGHT;
    bool gpu = true; // draw Miis on the GPU when libshacccg.suprx is installed

    static Settings& get();
    void load();
    void save() const;
};
