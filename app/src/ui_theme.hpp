#pragma once

#include <borealis.hpp>

// The app's look: light (light gray pages, blue selection) or dark (dark
// gray pages, green selection), loosely modeled on the Switch 2 Mii
// editor. Colors are read when drawing, so switching applies at once.
namespace ui {

// DARK: dark with green highlights; DARK_BLUE: dark with the light theme's blue
enum class Theme { LIGHT, DARK, DARK_BLUE };
inline bool isDark(Theme t) { return t != Theme::LIGHT; }

// Sets borealis' colors and metrics for `theme`. Call once before
// brls::Application::init() (it also prepares both borealis themes) and
// again with applyVariant() after it to switch.
void applyTheme(Theme theme);
// Switches borealis to the light or dark variant (after init).
void applyVariant();
Theme theme();

NVGcolor background();
NVGcolor tile();
NVGcolor text();
NVGcolor textDim();
NVGcolor rule();
NVGcolor ring();
NVGcolor accent();

} // namespace ui
