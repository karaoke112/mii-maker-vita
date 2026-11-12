#include "views/controls.hpp"

#include <cmath>
#include <cstring>

static NVGcolor rgb(uint32_t c)
{
    return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// A grid tile that shows a rendered icon once HeadRenderer delivers it.
// Icons are requested lazily, when the tile is first drawn, so only what is
// on screen gets rendered; the spec is re-checked every frame, so icons
// follow color changes.
class IconCell : public brls::Box
{
  public:
    IconCell(const mii_charinfo_t* mii, uint8_t value, int iconSize, OptionGrid::IconOf iconOf)
        : mii(mii), value(value), iconSize(iconSize), iconOf(std::move(iconOf))
    {
        std::memset(&requested.mii, 0, sizeof(requested.mii));
    }

    ~IconCell() override
    {
        HeadRenderer::instance().forget(this);
        if (nvgImage >= 0)
            nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        HeadRenderer& hr = HeadRenderer::instance();
        if (hr.available()) {
            HeadRenderer::Spec spec;
            spec.size = iconSize;
            spec.lowPriority = true;
            iconOf(value, *mii, &spec);
            if (!hasRequested || std::memcmp(&spec.mii, &requested.mii, sizeof(spec.mii)) != 0 ||
                spec.icon != requested.icon) {
                requested = spec;
                hasRequested = true;
                hr.request(this, spec, [this](HeadRenderer::Image img, int) {
                    image = img;
                    dirty = true;
                });
            }
        }
        if (image) {
            if (nvgImage < 0)
                nvgImage = nvgCreateImageRGBA(vg, iconSize, iconSize, 0, image->data());
            else if (dirty)
                nvgUpdateImage(vg, nvgImage, image->data());
            dirty = false;
            const float side = std::fmin(width, height) - 4;
            const float ix = x + (width - side) / 2, iy = y + (height - side) / 2;
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, side, side);
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, side, side, 0.0f, nvgImage, 1.0f));
            nvgFill(vg);
            return; // the icon replaces the number
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    const mii_charinfo_t* mii;
    uint8_t value;
    int iconSize;
    OptionGrid::IconOf iconOf;
    HeadRenderer::Spec requested;
    bool hasRequested = false;
    HeadRenderer::Image image;
    bool dirty = false;
    int nvgImage = -1;
};

std::vector<uint8_t> OptionGrid::range(mii_field_t field)
{
    std::vector<uint8_t> v;
    for (int i = mii_field_min(field); i <= mii_field_max(field); i++)
        v.push_back((uint8_t)i);
    return v;
}

OptionGrid::OptionGrid(mii_charinfo_t* mii, mii_field_t field, std::vector<uint8_t> items, int columns,
    float cellSize, ColorOf colorOf, LabelOf labelOf, std::function<void()> onChange, IconOf iconOf)
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
        brls::Box* cell = iconOf && !colorOf
            ? new IconCell(this->mii, value, 96, iconOf)
            : new brls::Box();
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
