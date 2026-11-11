#include "views/controls.hpp"

#include <cmath>

static NVGcolor rgb(uint32_t c)
{
    return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

std::vector<uint8_t> OptionGrid::range(mii_field_t field)
{
    std::vector<uint8_t> v;
    for (int i = mii_field_min(field); i <= mii_field_max(field); i++)
        v.push_back((uint8_t)i);
    return v;
}

OptionGrid::OptionGrid(mii_charinfo_t* mii, mii_field_t field, std::vector<uint8_t> items, int columns,
    float cellSize, ColorOf colorOf, LabelOf labelOf, std::function<void()> onChange)
    : brls::Box(brls::Axis::COLUMN)
    , mii(mii)
    , field(field)
    , values(std::move(items))
    , onChange(std::move(onChange))
{
    brls::Box* row = nullptr;
    for (size_t i = 0; i < values.size(); i++) {
        if (i % columns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(8);
            this->addView(row);
        }
        const uint8_t value = values[i];
        auto* cell = new brls::Box();
        cell->setDimensions(labelOf && !colorOf ? cellSize * 1.6f : cellSize, cellSize);
        cell->setMarginRight(8);
        cell->setCornerRadius(colorOf ? cellSize / 2 : 10);
        cell->setHighlightCornerRadius(colorOf ? cellSize / 2 : 12);
        cell->setFocusable(true);
        cell->setJustifyContent(brls::JustifyContent::CENTER);
        cell->setAlignItems(brls::AlignItems::CENTER);
        cell->setBackgroundColor(colorOf ? rgb(colorOf(value)) : nvgRGB(255, 255, 255));
        if (!colorOf) {
            auto* label = new brls::Label();
            label->setText(labelOf ? labelOf(value) : std::to_string(value + 1));
            label->setFontSize(labelOf ? 18 : 20);
            label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            cell->addView(label);
        }
        cell->registerClickAction([this, value](brls::View*) {
            mii_field_set(this->mii, this->field, value);
            this->refresh();
            if (this->onChange)
                this->onChange();
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));
        row->addView(cell);
        cells.push_back(cell);
    }
    refresh();
}

void OptionGrid::refresh()
{
    const uint8_t current = mii_field_get(mii, field);
    for (size_t i = 0; i < cells.size(); i++) {
        const bool selected = values[i] == current;
        cells[i]->setBorderColor(selected ? nvgRGB(255, 160, 0) : nvgRGBA(0, 0, 0, 40));
        cells[i]->setBorderThickness(selected ? 4.0f : 1.0f);
    }
}

FieldSlider::FieldSlider(mii_charinfo_t* mii, mii_field_t field, const std::string& title,
    std::function<void()> onChange)
    : brls::Box(brls::Axis::ROW)
{
    const int lo = mii_field_min(field), hi = mii_field_max(field);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setHeight(56);
    this->setMarginBottom(6);

    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(20);
    label->setWidth(150);
    this->addView(label);

    auto* slider = new brls::Slider();
    slider->setGrow(1);
    slider->setStep(1.0f / (float)(hi - lo));
    slider->setProgress((float)(mii_field_get(mii, field) - lo) / (float)(hi - lo));
    this->addView(slider);

    value = new brls::Label();
    value->setFontSize(20);
    value->setWidth(48);
    value->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
    value->setText(std::to_string(mii_field_get(mii, field)));
    this->addView(value);

    slider->getProgressEvent()->subscribe([this, mii, field, lo, hi, onChange](float progress) {
        int v = lo + (int)std::lround(progress * (float)(hi - lo));
        v = mii_field_set(mii, field, v);
        this->value->setText(std::to_string(v));
        if (onChange)
            onChange();
    });
}

brls::Label* makeSectionTitle(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(22);
    label->setMarginTop(14);
    label->setMarginBottom(10);
    label->setTextColor(nvgRGB(90, 90, 110));
    return label;
}
