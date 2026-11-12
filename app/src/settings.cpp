#include "settings.hpp"

#include "paths.hpp"

#include <cstdio>
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
            theme = !std::strcmp(value, "dark") ? ui::Theme::DARK : ui::Theme::LIGHT;
        else if (!std::strcmp(key, "renderer"))
            gpu = std::strcmp(value, "software") != 0;
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
    std::fprintf(f, "theme=%s\n", theme == ui::Theme::DARK ? "dark" : "light");
    std::fprintf(f, "renderer=%s\n", gpu ? "gpu" : "software");
    std::fprintf(f, "qr=%s\n", qrSwitch2 ? "switch2" : "3ds");
    std::fclose(f);
}
