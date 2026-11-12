#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <vector>

// Home screen: every saved Mii as a card, with "Add Mii" (start from
// scratch, import, ...) and "Send Mii" (export, ...) menus.
//   A: edit   Square: delete
class LibraryActivity : public brls::Activity
{
  public:
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void rebuild();
    void importMiis();
    void exportMii(int index);
    void showAddMenu();
    void showSendMenu();
    void showSettings();
    void rebuildForTheme();
    enum class Pick { NONE, EXPORT };
    void setPick(Pick mode);
    void openEditor(const mii_charinfo_t& start);

    brls::Box* grid = nullptr;
    brls::View* createButton = nullptr;
    brls::Label* empty = nullptr;
    brls::Label* pickLabel = nullptr;
    Pick pick = Pick::NONE;
    std::vector<brls::View*> cards;
    std::vector<mii_charinfo_t> miis;
};
