#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <vector>

// Home screen: every saved Mii as a card, plus "create" buttons.
//   A: edit   X: delete   Y: export (.charinfo / .nfsd / .ffsd)
class LibraryActivity : public brls::Activity
{
  public:
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void rebuild();
    void importMiis();
    void openEditor(const mii_charinfo_t& start);

    brls::Box* grid = nullptr;
    brls::View* createButton = nullptr;
    brls::Label* empty = nullptr;
    std::vector<mii_charinfo_t> miis;
};
