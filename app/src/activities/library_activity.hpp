#pragma once

#include <borealis.hpp>

#include "views/mii_preview.hpp"
#include <mii/mii.h>

#include <memory>
#include <vector>

// Home screen: every saved Mii as a card, with "Add Mii" (start from
// scratch, import, ...) and "Send Mii" (export, ...) menus.
//   A: edit   Square: delete
class LibraryActivity : public brls::Activity
{
  public:
    brls::View* createContentView() override;
    void onContentAvailable() override;
    // back on top: show Miis that arrived while another screen was open
    void onResume() override
    {
        if (stale) {
            stale = false;
            rebuild();
        }
    }

  private:
    void rebuild();
    void importMiis();
    void exportMii(int index);
    void addScanned(const mii_charinfo_t& mii);
    void showAddMenu();
    void showSendMenu();
    void showReceiveMenu();
    void showSettings(int focus = 0);
    void rebuildForTheme();
    enum class Pick { NONE, EXPORT, QR, SEND, KID, PARENT1, PARENT2 };
    void setPick(Pick mode);
    void openEditor(const mii_charinfo_t& start);
    void openView(int index);
    void upRoutes();
    void buttonRoutes();
    bool busy() const { return pick != Pick::NONE || held >= 0; }

    // Moving Miis: Triangle picks the Mii up; it travels with the cursor;
    // Cross puts it down there, Circle puts it back, Triangle swaps it with
    // the Mii under the cursor and carries that one instead.
    void cardActions(int index);
    void startHold(int index);
    void showHeldAt(int index);
    void endHold(bool place);
    void swapHeld(int index);
    void reloadOrder();
    int held = -1;   // where the held Mii is in the library, or -1
    int heldAt = -1; // the card under the cursor
    mii_charinfo_t heldMii {};
    std::unique_ptr<MiiPreview> floating; // the held Mii, drawn lifted
    std::vector<std::vector<brls::ActionIdentifier>> cardActionIds;

    brls::Box* grid = nullptr;
    brls::View* createButton = nullptr;
    std::vector<brls::View*> topButtons; // Add, Send, Receive
    bool batchingHints = false;           // cardActions for every card: one hints update
    brls::View* rootFrame = nullptr; // holds the START (settings) action
    brls::Label* countLabel = nullptr;
    bool settingsShown = false;
    void settingsAction(bool shown);
    void updateSettingsAction();
    void allCardActions();
    brls::Label* empty = nullptr;
    brls::Label* pickLabel = nullptr;
    Pick pick = Pick::NONE;
    // Child Generator: the first parent chosen
    mii_charinfo_t parentA{};
    int parentAIndex = -1;
    std::string parent2Prompt() const;
    bool stale = false;
    std::vector<brls::View*> cards;
    std::vector<brls::View*> thumbs; // the cards' previews
    void thumbPriorities(int focus);
    std::vector<mii_charinfo_t> miis;
};
