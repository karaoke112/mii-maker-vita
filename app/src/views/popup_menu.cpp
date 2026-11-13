#include "views/popup_menu.hpp"

#include "ui_theme.hpp"

using namespace brls::literals;

namespace {

// One row: its name, what is set (on the right), rules above and below
// (as the Switch draws them), the focus frame around it.
class PopupRow : public brls::Box
{
  public:
    explicit PopupRow(const PopupMenu::Item& item)
        : brls::Box(brls::Axis::ROW)
        , item(item)
    {
        this->setHeight(62);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 20, 0, 20);
        this->setFocusable(true);
        this->setHighlightCornerRadius(4);
        label = new brls::Label();
        label->setFontSize(24);
        label->setGrow(1);
        this->addView(label);
        if (item.value) {
            value = new brls::Label();
            value->setFontSize(22);
            this->addView(value);
        }
        if (item.swatch) {
            swatch = new brls::Rectangle(nvgRGB(0, 0, 0));
            swatch->setDimensions(34, 34);
            swatch->setCornerRadius(6);
            swatch->setMarginLeft(item.value ? 12 : 0);
            this->addView(swatch);
        }
        refresh();
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    PopupMenu::Item item;
    brls::Label* label = nullptr;
    brls::Label* value = nullptr;
    brls::Rectangle* swatch = nullptr;
    bool first = false;

    void refresh()
    {
        setIfChanged(label, item.text());
        if (value) {
            setIfChanged(value, item.value());
            value->setTextColor(item.off && item.off() ? ui::textDim() : ui::accent());
        }
        if (swatch) {
            const uint32_t c = item.swatch();
            swatch->setColor(nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        }
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        refresh(); // what is set may have changed (a selector, a toggle)
        nvgBeginPath(vg);
        nvgRect(vg, x, y + height - 1.5f, width, 1.5f);
        if (first)
            nvgRect(vg, x, y, width, 1.5f);
        nvgFillColor(vg, ui::rule());
        nvgFill(vg);
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    static void setIfChanged(brls::Label* l, const std::string& text)
    {
        if (l->getFullText() != text)
            l->setText(text);
    }
};

// Dims the screen behind the panel.
class Backdrop : public brls::Box
{
  public:
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 140));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }
};

} // namespace

PopupMenu::PopupMenu(std::string title, std::vector<Item> items, int focus)
    : title(std::move(title))
    , items(std::move(items))
    , focus(focus)
{
}

PopupMenu::Item PopupMenu::item(const std::string& text, std::function<void()> action)
{
    return Item { [text] { return text; }, std::move(action), false };
}

brls::Box* PopupMenu::row(const Item& item, bool first)
{
    auto* r = new PopupRow(item);
    r->first = first;
    const Item it = item;
    r->registerClickAction([it](brls::View*) {
        if (it.action)
            it.action();
        return true;
    });
    return r;
}

brls::View* PopupMenu::createContentView()
{
    auto* root = new Backdrop();
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);

    auto* panel = new brls::Box(brls::Axis::COLUMN);
    panel->setWidth(560);
    panel->setPadding(24, 30, 30, 30);
    panel->setBackgroundColor(ui::background());
    panel->setCornerRadius(8);

    auto* heading = new brls::Label();
    heading->setText(title);
    heading->setFontSize(28);
    heading->setMarginBottom(14);
    panel->addView(heading);

    for (size_t i = 0; i < items.size(); i++) {
        auto* row = new PopupRow(items[i]);
        row->first = i == 0;
        const Item it = items[i];
        row->registerClickAction([it](brls::View*) {
            if (it.keepOpen) {
                it.action();
                return true;
            }
            auto action = it.action;
            // close the menu, then act (the action may open another screen)
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [action] {
                brls::sync([action] { action(); });
            });
            return true;
        });
        panel->addView(row);
    }
    panel->setDefaultFocusedIndex(1 + focus); // after the heading
    root->addView(panel);
    // a tap beside the panel closes the menu
    root->addGestureRecognizer(new brls::TapGestureRecognizer([panel](brls::TapGestureStatus status, brls::Sound*) {
        if (status.state == brls::GestureState::END && !panel->getFrame().pointInside(status.position))
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
    }));
    root->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    return root;
}
