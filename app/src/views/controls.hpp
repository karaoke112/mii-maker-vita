#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include "head_renderer.hpp"

#include <functional>
#include <string>
#include <vector>

// A grid of choices bound to one Mii field: parts or a color palette.
// Laid out in the same order as the Switch editor (see mii_editor_grid), so
// muscle memory carries over. The chosen tile gets a blue frame, and it is
// where focus lands when the grid is shown.
class OptionGrid : public brls::Box
{
  public:
    using ColorOf = std::function<uint32_t(uint8_t value)>;
    using LabelOf = std::function<std::string(uint8_t value)>;
    // Fills in what to render for `value` (given the current Mii).
    using IconOf = std::function<void(uint8_t value, const mii_charinfo_t& current, HeadRenderer::Spec* spec)>;
    // Draws vector art under the icon (or instead of it) in the tile's square.
    using PaintOf = std::function<void(NVGcontext* vg, uint8_t value, float x, float y, float size)>;

    struct Options {
        int columns = 6;
        float cell = 96.0f;
        float gap = 12.0f;
        ColorOf colorOf;  // color swatches
        LabelOf labelOf;  // text tiles (wider)
        IconOf iconOf;    // rendered icons
        PaintOf paint;    // vector art
        float iconScale = 1.0f; // icon size relative to the tile
        float iconDy = 0.0f;    // icon offset, relative to the tile
        bool noneIcon = false;  // value 0 means "none": show the "none" icon
    };

    OptionGrid(mii_charinfo_t* mii, mii_field_t field, std::vector<uint8_t> items, Options options,
        std::function<void()> onChange);

    // Items 0..max(field) in natural order.
    static std::vector<uint8_t> range(mii_field_t field);

    brls::View* getDefaultFocus() override;

  private:
    void refresh();

    mii_charinfo_t* mii;
    mii_field_t field;
    std::vector<uint8_t> values;
    std::vector<class GridCell*> cells;
    std::function<void()> onChange;
    Options opt;
};

// A slider over a numeric field's full range, with a title or a pictogram
// at each end.
class FieldSlider : public brls::Box
{
  public:
    FieldSlider(mii_charinfo_t* mii, mii_field_t field, const std::string& title, std::function<void()> onChange);
    // Pictograms at both ends instead of a title.
    FieldSlider(mii_charinfo_t* mii, mii_field_t field, brls::View* lowEnd, brls::View* highEnd,
        std::function<void()> onChange);

  private:
    brls::View* makeSlider(mii_charinfo_t* mii, mii_field_t field, std::function<void()> onChange);
};

// The row of tabs across the top of a category page, with the L and R
// button glyphs at the ends and a blue underline under the open tab.
class TabBar : public brls::Box
{
  public:
    TabBar(const std::vector<std::string>& names, std::function<void(int)> onSelect);
    void setActive(int index);
    int count() const { return (int)tabs.size(); }
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    std::vector<brls::Box*> tabs;
    std::vector<brls::Label*> labels;
    brls::Label* left;
    brls::Label* right;
    int active = 0;
};

// A big centered sentence above a page's choices ("Pick a ...").
brls::Label* makePrompt(const std::string& text);

// Section heading used inside pages.
brls::Label* makeSectionTitle(const std::string& text);
