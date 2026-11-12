#include "views/controls.hpp"

#include <cstdio>

#include "ui_theme.hpp"
#include "views/icon.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

static NVGcolor rgb(uint32_t c)
{
    return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// One tile of an OptionGrid: a color swatch, or a light tile with vector
// art, a rendered icon (requested from HeadRenderer the first time the tile
// is drawn, so only what is on screen gets rendered) or a label.
class GridCell : public brls::Box
{
  public:
    GridCell(const mii_charinfo_t* mii, uint8_t value, const OptionGrid::Options& opt)
        : mii(mii)
        , value(value)
        , opt(opt)
    {
        std::memset(&requested.mii, 0, sizeof(requested.mii));
        iconSize = (int)std::lround(opt.cell * 1.15f * opt.iconScale);
        if (iconSize < 32)
            iconSize = 32;
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        if (!opt.colorOf && !opt.paint) {
            auto* label = new brls::Label();
            label->setText(opt.labelOf ? opt.labelOf(value) : std::to_string(value + 1));
            label->setFontSize(opt.labelOf ? 22 : 20);
            label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            this->addView(label);
        }
    }

    ~GridCell() override
    {
        HeadRenderer::instance().forget(this);
        if (nvgImage >= 0)
            nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
    }

    void setSelected(bool s) { selected = s; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, opt.colorOf ? rgb(opt.colorOf(value)) : ui::tile());
        nvgFill(vg);

        const float side = std::fmin(width, height);
        const float sx = x + (width - side) / 2, sy = y + (height - side) / 2;
        if (opt.paint)
            opt.paint(vg, value, sx, sy, side);

        bool drewIcon = false;
        if (opt.noneValue >= 0 && value == opt.noneValue) {
            const float s = side * 0.42f;
            ::drawIcon(vg, "none", sx + (side - s) / 2, sy + (side - s) / 2, s, ui::ring());
            drewIcon = true;
        } else if (opt.iconOf) {
            drewIcon = drawIcon(vg, sx, sy, side);
        }
        if (!drewIcon && !opt.colorOf && !opt.paint)
            brls::Box::draw(vg, x, y, width, height, style, ctx);

        if (selected) {
            nvgBeginPath(vg);
            nvgRect(vg, x + 2, y + 2, width - 4, height - 4);
            nvgStrokeColor(vg, ui::accent());
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
            if (opt.colorOf) {
                nvgBeginPath(vg);
                nvgRect(vg, x + 5.5f, y + 5.5f, width - 11, height - 11);
                nvgStrokeColor(vg, nvgRGB(255, 255, 255));
                nvgStrokeWidth(vg, 3);
                nvgStroke(vg);
            }
        }
    }

  private:
    bool drawIcon(NVGcontext* vg, float sx, float sy, float side)
    {
        HeadRenderer& hr = HeadRenderer::instance();
        if (hr.available()) {
            HeadRenderer::Spec spec;
            spec.size = iconSize;
            spec.lowPriority = true;
            opt.iconOf(value, *mii, &spec);
            // re-checked every frame, so icons follow the Mii's colors
            if (!hasRequested || std::memcmp(&spec.mii, &requested.mii, sizeof(spec.mii)) != 0 ||
                spec.icon != requested.icon) {
                requested = spec;
                hasRequested = true;
                hr.request(this, spec, [this](HeadRenderer::Image img, int, int) {
                    image = img;
                    dirty = true;
                });
            }
        }
        if (!image)
            return false;
        if (nvgImage < 0)
            nvgImage = nvgCreateImageRGBA(vg, iconSize, iconSize, 0, image->data());
        else if (dirty)
            nvgUpdateImage(vg, nvgImage, image->data());
        dirty = false;
        const float s = (side - 6) * opt.iconScale;
        const float ix = sx + (side - s) / 2, iy = sy + (side - s) / 2 + opt.iconDy * side;
        nvgBeginPath(vg);
        nvgRect(vg, ix, iy, s, s);
        nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, s, s, 0.0f, nvgImage, 1.0f));
        nvgFill(vg);
        return true;
    }

    const mii_charinfo_t* mii;
    uint8_t value;
    const OptionGrid::Options& opt;
    int iconSize;
    bool selected = false;
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

OptionGrid::OptionGrid(mii_charinfo_t* mii, mii_field_t field, std::vector<uint8_t> items, Options options,
    std::function<void()> onChange)
    : brls::Box(brls::Axis::COLUMN)
    , mii(mii)
    , field(field)
    , values(std::move(items))
    , onChange(std::move(onChange))
    , opt(std::move(options))
{
    // cells keep a reference to `opt`; they are freed after it, but their
    // destructors do not touch it

    const float w = opt.labelOf && !opt.colorOf ? opt.cell * 2.4f : opt.cell;
    brls::Box* row = nullptr;
    for (size_t i = 0; i < values.size(); i++) {
        if (i % opt.columns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            if (i > 0)
                row->setMarginTop(opt.gap);
            this->addView(row);
        }
        const uint8_t value = values[i];
        auto* cell = new GridCell(this->mii, value, opt);
        cell->setDimensions(w, opt.cell);
        if (i % opt.columns != 0)
            cell->setMarginLeft(opt.gap);
        cell->setFocusable(true);
        cell->setHighlightCornerRadius(3);
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
    // Up / down go to the tile in the same column (the last one of a
    // shorter row), not to whichever tile of that row was focused last.
    const int cols = opt.columns, count = (int)cells.size();
    for (int i = 0; i < count; i++) {
        const int col = i % cols, rowStart = i - col;
        if (rowStart > 0)
            cells[i]->setCustomNavigationRoute(brls::FocusDirection::UP, cells[rowStart - cols + col]);
        if (rowStart + cols < count)
            cells[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN,
                cells[std::min(rowStart + cols + col, count - 1)]);
    }
    refresh();
}

void OptionGrid::refresh()
{
    const uint8_t current = mii_field_get(mii, field);
    shown = current;
    for (size_t i = 0; i < cells.size(); i++)
        cells[i]->setSelected(values[i] == current);
}

void OptionGrid::updatePriorities()
{
    if (!opt.iconOf)
        return;
    int from = -1;
    brls::View* focus = brls::Application::getCurrentFocus();
    for (size_t i = 0; i < cells.size(); i++)
        if (cells[i] == focus)
            from = (int)i;
    if (from < 0) {
        const uint8_t current = mii_field_get(mii, field);
        for (size_t i = 0; i < values.size(); i++)
            if (values[i] == current)
                from = (int)i;
    }
    if (from < 0)
        from = 0;
    if (from == prioritiesFrom)
        return;
    prioritiesFrom = from;
    HeadRenderer& hr = HeadRenderer::instance();
    const int cols = opt.columns, fr = from / cols, fc = from % cols;
    for (size_t i = 0; i < cells.size(); i++) {
        const int r = (int)i / cols, c = (int)i % cols;
        // rows count more than columns: the screen shows a few rows at a time
        hr.setPriority(cells[i], 2 * std::abs(r - fr) + std::abs(c - fc));
    }
}

brls::View* OptionGrid::getDefaultFocus()
{
    // While the cursor is in the grid, that tile (scrolling frames center on
    // the default focus); otherwise the chosen tile.
    brls::View* focus = brls::Application::getCurrentFocus();
    for (GridCell* c : cells)
        if (c == focus)
            return c;
    const uint8_t current = mii_field_get(mii, field);
    for (size_t i = 0; i < cells.size(); i++) {
        if (values[i] == current)
            return cells[i];
    }
    return cells.empty() ? nullptr : cells[0];
}

// ---- FieldSlider -------------------------------------------------------

// A slider over a whole-number field: each press of left / right moves one
// step; holding speeds up so the whole range takes about a second; touch
// drags it. The value always snaps to whole steps.
class UnitSlider : public brls::Box
{
  public:
    UnitSlider(mii_charinfo_t* mii, mii_field_t field, std::function<void()> onChange)
        : mii(mii)
        , field(field)
        , lo(mii_field_min(field))
        , hi(mii_field_max(field))
        , onChange(std::move(onChange))
    {
        this->setHeight(48);
        this->setFocusable(true);
        this->setHideHighlight(true); // the knob shows focus
        // left / right adjust the value instead of moving focus
        for (brls::ControllerButton b : { brls::BUTTON_NAV_LEFT, brls::BUTTON_NAV_RIGHT })
            this->registerAction("", b, [](brls::View*) { return true; }, true, true, brls::SOUND_NONE);
        this->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound*) {
                brls::Application::giveFocus(this);
                setFromX(status.position.x);
            },
            brls::PanAxis::HORIZONTAL));
        this->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound*) { setFromX(status.position.x); }));
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        if (this->isFocused())
            poll();
        const float t = hi > lo ? (float)(mii_field_get(mii, field) - lo) / (float)(hi - lo) : 0.0f;
        const float x0 = x + kKnob, x1 = x + width - kKnob, cy = y + height / 2;
        const float kx = x0 + (x1 - x0) * t;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x0, cy - 3.5f, x1 - x0, 7, 3.5f);
        nvgFillColor(vg, ui::rule());
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x0, cy - 3.5f, kx - x0, 7, 3.5f);
        nvgFillColor(vg, ui::accent());
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, kx, cy, kKnob - 4);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        nvgStrokeWidth(vg, this->isFocused() ? 4.0f : 2.0f);
        nvgStrokeColor(vg, this->isFocused() ? ui::accent() : ui::ring());
        nvgStroke(vg);
    }

  private:
    static constexpr float kKnob = 22.0f;

    void set(int v)
    {
        v = std::max(lo, std::min(hi, v));
        if (v == mii_field_get(mii, field))
            return;
        mii_field_set(mii, field, v);
        if (onChange)
            onChange();
    }

    void setFromX(float px)
    {
        const float x0 = this->getX() + kKnob, x1 = this->getX() + this->getWidth() - kKnob;
        const float t = x1 > x0 ? (px - x0) / (x1 - x0) : 0.0f;
        set(lo + (int)std::lround(std::max(0.0f, std::min(1.0f, t)) * (float)(hi - lo)));
    }

    void poll()
    {
        const auto& st = brls::Application::getControllerState();
        const int dir = (st.buttons[brls::BUTTON_NAV_RIGHT] ? 1 : 0) - (st.buttons[brls::BUTTON_NAV_LEFT] ? 1 : 0);
        const brls::Time now = brls::getCPUTimeUsec();
        if (dir == 0) {
            heldDir = 0;
            return;
        }
        if (dir != heldDir) { // a new press: one step
            heldDir = dir;
            heldSince = lastTick = now;
            carry = 0.0f;
            set(mii_field_get(mii, field) + dir);
            return;
        }
        const float held = (float)(now - heldSince) / 1e6f;
        const float dt = (float)(now - lastTick) / 1e6f;
        lastTick = now;
        if (held < 0.3f)
            return;
        // held: about one second end to end, at least 10 steps a second
        const float speed = std::max(10.0f, (float)(hi - lo) / 1.0f);
        carry += speed * dt;
        const int steps = (int)carry;
        if (steps > 0) {
            carry -= (float)steps;
            set(mii_field_get(mii, field) + dir * steps);
        }
    }

    mii_charinfo_t* mii;
    mii_field_t field;
    int lo, hi;
    std::function<void()> onChange;
    int heldDir = 0;
    brls::Time heldSince = 0, lastTick = 0;
    float carry = 0.0f;
};

void FieldValue::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%d", mii_field_get(mii, field));
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, 20);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textDim());
    nvgText(vg, x + width, y + height / 2, text, nullptr);
}

brls::View* FieldSlider::makeSlider(mii_charinfo_t* mii, mii_field_t field, std::function<void()> onChange)
{
    auto* slider = new UnitSlider(mii, field, std::move(onChange));
    slider->setGrow(1);
    return slider;
}

FieldSlider::FieldSlider(mii_charinfo_t* mii, mii_field_t field, const std::string& title,
    std::function<void()> onChange)
    : brls::Box(brls::Axis::ROW)
{
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setHeight(64);
    this->setMarginBottom(8);

    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(22);
    label->setWidth(170);
    this->addView(label);
    this->addView(makeSlider(mii, field, std::move(onChange)));
    auto* value = new FieldValue(mii, field);
    value->setMarginLeft(14);
    this->addView(value);
}

FieldSlider::FieldSlider(mii_charinfo_t* mii, mii_field_t field, brls::View* lowEnd, brls::View* highEnd,
    std::function<void()> onChange)
    : brls::Box(brls::Axis::ROW)
{
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setHeight(96);
    this->setMarginBottom(16);
    lowEnd->setMarginRight(16);
    highEnd->setMarginLeft(16);
    this->addView(lowEnd);
    this->addView(makeSlider(mii, field, std::move(onChange)));
    this->addView(highEnd);
}

// ---- TabBar ------------------------------------------------------------

static brls::Label* glyphLabel(const char* text)
{
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(30);
    l->setWidth(60);
    l->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    return l;
}

TabBar::TabBar(const std::vector<std::string>& names, std::function<void(int)> onSelect)
    : brls::Box(brls::Axis::ROW)
{
    this->setHeight(76);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(0, 16, 0, 16);

    left = glyphLabel("");
    this->addView(left);
    for (size_t i = 0; i < names.size(); i++) {
        auto* tab = new brls::Box();
        tab->setGrow(1);
        tab->setHeight(76);
        tab->setJustifyContent(brls::JustifyContent::CENTER);
        tab->setAlignItems(brls::AlignItems::CENTER);
        auto* label = new brls::Label();
        label->setText(names[i]);
        label->setFontSize(26);
        tab->addView(label);
        const int index = (int)i;
        tab->addGestureRecognizer(new brls::TapGestureRecognizer(tab, [onSelect, index] { onSelect(index); }));
        this->addView(tab);
        tabs.push_back(tab);
        labels.push_back(label);
    }
    right = glyphLabel("");
    this->addView(right);
    setActive(0);
}

void TabBar::setActive(int index)
{
    active = index;
    for (size_t i = 0; i < labels.size(); i++)
        labels[i]->setTextColor((int)i == active ? ui::accent() : ui::text());
    left->setTextColor(active > 0 ? ui::text() : ui::rule());
    right->setTextColor(active + 1 < (int)labels.size() ? ui::text() : ui::rule());
}

void TabBar::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx)
{
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    nvgBeginPath(vg);
    nvgRect(vg, x, y + height - 1.5f, width, 1.5f);
    nvgFillColor(vg, ui::rule());
    nvgFill(vg);

    if (active >= 0 && active < (int)tabs.size()) {
        brls::Box* tab = tabs[active];
        const float lw = std::fmin(tab->getWidth() * 0.8f, labels[active]->getWidth() + 28.0f);
        const float lx = tab->getX() + (tab->getWidth() - lw) / 2;
        nvgBeginPath(vg);
        nvgRect(vg, lx, y + height - 6, lw, 4);
        nvgFillColor(vg, ui::accent());
        nvgFill(vg);
    }
}

// ---- labels ------------------------------------------------------------

brls::Label* makePrompt(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(26);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMarginBottom(28);
    return label;
}

brls::Label* makeSectionTitle(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(22);
    label->setMarginTop(6);
    label->setMarginBottom(12);
    return label;
}
