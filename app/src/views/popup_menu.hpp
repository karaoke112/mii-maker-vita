#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

// A small menu over the current screen: a title and a list of rows. Picking
// a row closes the menu and runs its action, or (for settings) runs it and
// keeps the menu open. B closes the menu.
class PopupMenu : public brls::Activity
{
  public:
    // A row: its name on the left and, for settings, what is set on the
    // right (as the Switch's settings show it): a value in the accent color
    // (dimmed when `off`, as a switched-off toggle) or a color swatch. All
    // are re-read as the menu is drawn, so they follow changes.
    struct Item {
        std::function<std::string()> text;
        std::function<void()> action;
        bool keepOpen = false;
        std::function<std::string()> value;  // optional
        std::function<bool()> off;           // optional: the value greyed out
        std::function<uint32_t()> swatch;    // optional: 0xRRGGBB on the right
    };

    // `focus`: the row the cursor starts on.
    PopupMenu(std::string title, std::vector<Item> items, int focus = 0);
    static Item item(const std::string& text, std::function<void()> action);

    brls::View* createContentView() override;
    bool isTranslucent() override { return true; }

  private:
    std::string title;
    std::vector<Item> items;
    int focus;
};
