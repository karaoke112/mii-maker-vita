#pragma once

#include "ui_theme.hpp"

#include <cstdint>

// App settings, kept in ux0:data/mii-maker-vita/settings.txt as key=value
// lines (unknown keys are ignored, missing ones keep their defaults).
struct Settings {
    ui::Theme theme = ui::Theme::LIGHT;
    // QR codes in the Switch 2 format (keeps Switch colors) or the plain
    // 3DS / Wii U one
    bool qrSwitch2 = true;
    // skin tone (0-9) of new Miis and of the face icons
    uint8_t skinTone = 0;
    // background music: music::Track (0 none)
    int music = 0;

    static Settings& get();
    void load();
    void save() const;
};
