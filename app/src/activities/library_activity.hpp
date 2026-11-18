#pragma once

#include <borealis.hpp>

#include "mii_store.hpp"
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
        clearNextPlace(); // (a new Mii started on an empty place, or not saved)
        if (stale) {
            stale = false;
            rebuild();
        }
    }

  private:
    static void clearNextPlace();
    void rebuild();
    void importMiis();
    void exportMii(int index);
    void addScanned(const mii_charinfo_t& mii, const mii_ver3_info_t& info);
    void showAddMenu();
    void showSendMenu();
    void showReceiveMenu();
    void showSettings(int focus = 0);
    // View options: how the lists are shown (sorted, some only), not how
    // they are kept
    enum Sort { SORT_OWN, SORT_NAME, SORT_COLOR, SORT_HEIGHT, SORT_COUNT };
    enum Show { SHOW_ALL, SHOW_FAVORITES, SHOW_MALE, SHOW_FEMALE, SHOW_COUNT };
    void showViewOptions();
    // sorted or only some shown: no moving Miis, no empty places
    static bool arranged();
    brls::View* viewButton = nullptr;
    std::vector<int> storeAt; // each card's index in the list (-1: the empty card for a copy)
    void rebuildForTheme();
    enum class Pick { NONE, EXPORT, QR, SEND, DELETE, KID, PARENT1, PARENT2 };
    // Sending and deleting choose several Miis; START goes on with them
    bool choosingSeveral() const { return pick == Pick::SEND || pick == Pick::DELETE; }
    std::vector<bool> chosen;
    void toggleChosen(int index);
    int chosenCount() const;
    void finishChoosing();
    void setPick(Pick mode);
    void openEditor(const mii_charinfo_t& start);
    void openView(int index);
    // the list shown: the app's own, the 3DS's in Azahar, the Wii's (L / R)
    enum Tab { TAB_VITA, TAB_3DS, TAB_WII, TAB_COUNT };
    int tab = TAB_VITA;
    MiiStore& store() { return store(tab); }
    static MiiStore& store(int t);
    static std::string tabName(int t);  // (the L / R hints)
    static std::string tabTitle(int t); // (the list's title)
    void switchTab(int to);
    std::string fullMessage() const;
    void importFavorites();
    void focusCard(int index);
    void tabActions();
    brls::AppletFrame* appletFrame = nullptr;
    void upRoutes();
    void buttonRoutes();
    bool busy() const { return pick != Pick::NONE || holding; }

    // Moving Miis: Triangle picks the Mii up; it travels with the cursor;
    // Cross puts it down there, Circle puts it back, Triangle swaps it with
    // the Mii under the cursor and carries that one instead. L / R take it
    // to the other list, where Cross puts a copy of it.
    void cardActions(int index);
    void startHold(int index);
    void holdLook();
    void showHeldAt(int index);
    void endHold(bool place);
    bool copyHeld(int to);
    void swapHeld(int index);
    void refreshCards();
    bool holding = false;
    int holdFromTab = TAB_VITA; // the list it was picked up from
    int holdFrom = -1;          // and where in it
    bool copying() const { return holding && holdFromTab != tab; }
    int held = -1;   // where the held Mii is in the list shown, or -1 (from the other list)
    int heldAt = -1; // the card under the cursor
    mii_charinfo_t heldMii {};
    std::unique_ptr<MiiPreview> floating; // the held Mii, drawn lifted
    std::vector<std::vector<brls::ActionIdentifier>> cardActionIds;
    int focusedCard = -1; // the card the cursor is on (only it has its buttons)
    int builtTab = -1;    // the list the cards were made for
    bool actionsPending = false; // (allCardActions, once the input is handled)

    brls::Box* grid = nullptr;
    brls::View* createButton = nullptr;
    std::vector<brls::View*> topButtons; // Add, Send, Receive, Settings
    brls::View* settingsButton = nullptr;
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
    void thumbPriorities(int focus);
    std::vector<mii_charinfo_t> miis; // one per card (an empty place: a default Mii, not shown)
    std::vector<bool> present;        // a Mii there (the 3DS's list has empty places)
    bool hasMii(int index) const { return index >= 0 && index < (int)present.size() && present[index]; }
    int addPlace = -1; // Cross on an empty place: the new Mii goes there
};
