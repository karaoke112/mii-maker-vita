#include "views/popup_menu.hpp"

#include "ui_theme.hpp"

using namespace brls::literals;

namespace {

// One row: its text, a rule underneath (not under the last), the focus frame around it.
class PopupRow : public brls::Box
{
  public:
    PopupRow()
        : brls::Box(brls::Axis::ROW)
    {
        this->setHeight(62);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 20, 0, 20);
        this->setFocusable(true);
        this->setHighlightCornerRadius(4);
        label = new brls::Label();
        label->setFontSize(24);
        this->addView(label);
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    brls::Label* label;
    bool last = false;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        if (!last) {
            nvgBeginPath(vg);
            nvgRect(vg, x, y + height - 1.5f, width, 1.5f);
            nvgFillColor(vg, ui::rule());
            nvgFill(vg);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
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
        auto* row = new PopupRow();
        row->last = i + 1 == items.size();
        row->label->setText(items[i].text());
        const Item it = items[i];
        row->registerClickAction([it, row](brls::View*) {
            if (it.keepOpen) {
                it.action();
                row->label->setText(it.text());
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
    root->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    return root;
}
