#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>
#include <vector>

// A Mii from the list on a screen of its own (Cross on a card): the whole
// Mii, turned with the right stick or a finger, its name, and what can be
// done with it: edit, change the expression or pose (shown here only), view
// its QR code, Kid-o-matic, delete. L / R show the Mii before / after it.
class MiiViewActivity : public brls::Activity
{
  public:
    // `onClosed(index, changed)`: the Mii shown last, and whether any Mii
    // was edited, deleted or added (Kid-o-matic) meanwhile.
    MiiViewActivity(std::vector<mii_charinfo_t> miis, int index, std::function<void(int, bool)> onClosed);

    brls::View* createContentView() override;

  private:
    void show(int index);
    void step(int delta);
    void edit();
    void remove();
    void close();

    std::vector<mii_charinfo_t> miis;
    int index;
    std::function<void(int, bool)> onClosed;
    bool changed = false;
    mii_charinfo_t current; // the preview points here
    float yaw = 0.0f, pitch = 0.0f;
    bool facingFront = false;
    int expression = 0, pose = 0; // kept from one Mii to the next
    brls::Label* name = nullptr;
    brls::Label* position = nullptr;
};
