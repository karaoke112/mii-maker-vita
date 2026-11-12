#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

// A small menu over the current screen: a title and a list of rows. Picking
// a row closes the menu and runs its action, or (for settings) runs it and
// keeps the menu open with the row's text refreshed. B closes the menu.
class PopupMenu : public brls::Activity
{
  public:
    struct Item {
        std::function<std::string()> text; // the row's text, re-read after each press
        std::function<void()> action;
        bool keepOpen = false;
    };

    PopupMenu(std::string title, std::vector<Item> items);
    static Item item(const std::string& text, std::function<void()> action);

    brls::View* createContentView() override;
    bool isTranslucent() override { return true; }

  private:
    std::string title;
    std::vector<Item> items;
};
