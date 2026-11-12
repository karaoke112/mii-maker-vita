#pragma once

#include <borealis.hpp>

// The editor's look: a light gray page, white-gray tiles, blue selection,
// thin gray rules. Loosely modeled on the Switch 2 Mii editor's layout.
namespace ui {

inline const NVGcolor kBackground = nvgRGB(235, 235, 235);
inline const NVGcolor kTile = nvgRGB(247, 247, 247);
inline const NVGcolor kText = nvgRGB(30, 30, 30);
inline const NVGcolor kTextDim = nvgRGB(150, 150, 150);
inline const NVGcolor kRule = nvgRGB(196, 196, 196);
inline const NVGcolor kRing = nvgRGB(190, 190, 190);
inline const NVGcolor kAccent = nvgRGB(10, 100, 220);

// Applies the colors and metrics above to borealis' light theme. Call
// before brls::Application::init().
void applyTheme();

} // namespace ui
