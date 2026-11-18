#include "settings.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string settingsPath()
{
    return paths::appDir() + "/settings.txt";
}

Settings& Settings::get()
{
    static Settings s;
    return s;
}

void Settings::load()
{
    FILE* f = std::fopen(settingsPath().c_str(), "r");
    if (!f)
        return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char* eq = std::strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char* key = line;
        char* value = eq + 1;
        value[std::strcspn(value, "\r\n")] = 0;
        if (!std::strcmp(key, "theme"))
            theme = !std::strcmp(value, "dark") ? ui::Theme::DARK
                : !std::strcmp(value, "dark2")  ? ui::Theme::DARK_BLUE
                                                : ui::Theme::LIGHT;
        else if (!std::strcmp(key, "skin"))
            skinTone = (uint8_t)std::min(9, std::max(0, std::atoi(value)));
        else if (!std::strcmp(key, "music"))
            music = std::min(3, std::max(0, std::atoi(value)));
        else if (!std::strcmp(key, "lookalikes"))
            curatedLookalikes = std::strcmp(value, "random") != 0;
        else if (!std::strcmp(key, "qr"))
            qrSwitch2 = std::strcmp(value, "3ds") != 0;
        else if (!std::strcmp(key, "sort")) {
            sortMode = std::min(4, std::max(0, std::atoi(value)));
            if (sortMode == 4) { // (Favorites First was an order: now a switch of its own)
                sortMode = 0;
                favoritesFirst = true;
            }
        } else if (!std::strcmp(key, "favorites_first"))
            favoritesFirst = std::atoi(value) != 0;
        else if (!std::strcmp(key, "show"))
            showMode = std::min(3, std::max(0, std::atoi(value)));
        else if (!std::strcmp(key, "sounds"))
            soundLevel = std::min(3, std::max(0, std::atoi(value)));
        else if (!std::strcmp(key, "music_volume"))
            musicLevel = std::min(3, std::max(1, std::atoi(value)));
    }
    std::fclose(f);
}

void Settings::save() const
{
    FILE* f = std::fopen(settingsPath().c_str(), "w");
    if (!f)
        return;
    std::fprintf(f, "theme=%s\n", theme == ui::Theme::DARK ? "dark" : theme == ui::Theme::DARK_BLUE ? "dark2" : "light");
    std::fprintf(f, "qr=%s\n", qrSwitch2 ? "switch2" : "3ds");
    std::fprintf(f, "skin=%d\n", skinTone);
    std::fprintf(f, "music=%d\n", music);
    std::fprintf(f, "lookalikes=%s\n", curatedLookalikes ? "curated" : "random");
    std::fprintf(f, "sort=%d\n", sortMode);
    std::fprintf(f, "show=%d\n", showMode);
    std::fprintf(f, "favorites_first=%d\n", favoritesFirst ? 1 : 0);
    std::fprintf(f, "sounds=%d\n", soundLevel);
    std::fprintf(f, "music_volume=%d\n", musicLevel);
    std::fclose(f);
}
