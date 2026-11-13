#pragma once

#include <borealis.hpp>

#include "mii_library.hpp"

#include <functional>
#include <vector>

// Import from SD Card: the Miis found in ux0:data/mii/import/, each to be
// imported (as it starts), left out (Cross: its file stays in import/) or
// deleted (Square: its file is removed). START goes on: the ones to import
// join the Mii list. Circle goes back and changes nothing.
class ImportActivity : public brls::Activity
{
  public:
    // `pending` from MiiLibrary::scanImports; `onDone(imported)` after START
    ImportActivity(std::vector<MiiLibrary::Pending> pending, int unreadable, std::function<void(int)> onDone);

    brls::View* createContentView() override;

  private:
    enum State { IMPORT, LEAVE, DELETE };
    void setState(int index, State s);
    void refreshHints();
    void finish();

    std::vector<MiiLibrary::Pending> pending; // the previews point into it
    std::vector<State> states;
    int unreadable;
    std::function<void(int)> onDone;
    std::vector<brls::View*> cards;
    brls::Label* summary = nullptr;
    brls::View* rootFrame = nullptr;
    bool batching = false;
};
