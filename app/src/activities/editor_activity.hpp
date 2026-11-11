#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>

// The Mii editor: live preview on the left, categories on the right, in the
// same order as the Switch editor. START saves, B asks before discarding.
class EditorActivity : public brls::Activity
{
  public:
    using OnSaved = std::function<void()>;

    EditorActivity(const mii_charinfo_t& start, OnSaved onSaved);

    brls::View* createContentView() override;

  private:
    using Fill = std::function<void(brls::Box*)>;
    brls::View* makeTab(Fill fill);
    void changed() { dirty = true; }
    bool save();
    bool close();

    brls::View* profileTab();
    brls::View* faceTab();
    brls::View* hairTab();
    brls::View* eyebrowTab();
    brls::View* eyeTab();
    brls::View* noseTab();
    brls::View* mouthTab();
    brls::View* facialHairTab();
    brls::View* glassesTab();
    brls::View* moleTab();
    brls::View* bodyTab();

    mii_charinfo_t mii;
    OnSaved onSaved;
    bool dirty = false;
};
