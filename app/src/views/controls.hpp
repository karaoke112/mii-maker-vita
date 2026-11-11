#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>
#include <string>
#include <vector>

// A grid of choices bound to one Mii field: parts (numbered tiles) or a
// color palette (swatches). Laid out in the same order as the Switch editor
// (see mii_editor_grid), so muscle memory carries over.
class OptionGrid : public brls::Box
{
  public:
    using ColorOf = std::function<uint32_t(uint8_t value)>;
    using LabelOf = std::function<std::string(uint8_t value)>;

    OptionGrid(mii_charinfo_t* mii, mii_field_t field, std::vector<uint8_t> items, int columns,
        float cellSize, ColorOf colorOf, LabelOf labelOf, std::function<void()> onChange);

    // Items 0..max(field) in natural order.
    static std::vector<uint8_t> range(mii_field_t field);

  private:
    void refresh();

    mii_charinfo_t* mii;
    mii_field_t field;
    std::vector<uint8_t> values;
    std::vector<brls::Box*> cells;
    std::function<void()> onChange;
};

// A labeled slider over a numeric field's full range.
class FieldSlider : public brls::Box
{
  public:
    FieldSlider(mii_charinfo_t* mii, mii_field_t field, const std::string& title, std::function<void()> onChange);

  private:
    brls::Label* value;
};

// Section heading used inside editor tabs.
brls::Label* makeSectionTitle(const std::string& text);
