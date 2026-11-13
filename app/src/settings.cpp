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
    std::fclose(f);
}
