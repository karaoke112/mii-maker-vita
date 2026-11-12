#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class IconView;

// What the editor's screens share: the Mii being edited and how far the
// preview is turned.
struct EditorSession {
    mii_charinfo_t mii;
    float yaw = 0.0f;
    bool dirty = false;
};

// The editor's categories, in the order of the menu (two columns, read
// left to right).
enum class Category {
    FACE,
    HAIR,
    EYEBROWS,
    EYES,
    NOSE,
    MOUTH,
    FACIAL_HAIR,
    MOLE,
    STYLE,
    BODY,
    GLASSES,
    FAVORITE_COLOR,
    COUNT
};

// The Mii editor's main screen: a large live preview on the left, and on the
// right the category menu, the nickname and a Done button. Each category
// opens its own page (CategoryActivity). Done or START saves; B asks before
// discarding changes.
class EditorActivity : public brls::Activity
{
  public:
    using OnSaved = std::function<void()>;

    EditorActivity(const mii_charinfo_t& start, OnSaved onSaved);

    brls::View* createContentView() override;
    void onResume() override;

  private:
    bool save();
    bool close();

    std::shared_ptr<EditorSession> session;
    OnSaved onSaved;
    IconView* favoriteStar = nullptr;
};

// One category's page: the preview stays on the left; on the right a tab
// bar (switched with L / R) over the choices.
class CategoryActivity : public brls::Activity
{
  public:
    CategoryActivity(std::shared_ptr<EditorSession> session, Category category);

    brls::View* createContentView() override;

  private:
    struct Tab {
        std::string name;
        std::function<brls::View*()> build;
    };
    void addTabs();
    void setTab(int index);
    static bool isInside(brls::View* view, brls::View* ancestor);

    std::shared_ptr<EditorSession> session;
    Category category;
    std::vector<Tab> tabs;
    class TabBar* tabBar = nullptr;
    brls::Box* body = nullptr;
    brls::View* current = nullptr;
    int active = -1;
};
