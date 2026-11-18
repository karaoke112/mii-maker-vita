#include "activities/library_activity.hpp"

#include "activities/child_activity.hpp"
#include "activities/editor_activity.hpp"
#include "activities/import_activity.hpp"
#include "activities/mii_view_activity.hpp"
#include "activities/qr_activity.hpp"
#include "activities/trade_activity.hpp"
#include "activities/start_activity.hpp"
#include "head_renderer.hpp"
#include "cfl3ds_store.hpp"
#include "wii_store.hpp"
#include "mii_library.hpp"
#include "music.hpp"
#include "mii_qr.hpp"
#include "settings.hpp"
#include "sound.hpp"

#include <cctype>
#include "ui_theme.hpp"
#include "views/popup_menu.hpp"
#include <mii/mii_editor.h>
#include "views/controls.hpp"
#include "views/icon.hpp"
#include "views/cull.hpp"
#include "views/mii_preview.hpp"

#include <cstring>
#include <memory>

using namespace brls::literals;

static constexpr int kColumns = 5;

// A Mii's card; while a Mii is held, the card under the cursor draws it
// lifted above itself.
class MiiCard : public brls::Box
{
  public:
    explicit MiiCard(const mii_charinfo_t* mii)
        : brls::Box(brls::Axis::COLUMN)
        , mii(mii)
    {
    }
    // Only cards on screen are drawn (see MiiRow), and only they hold a
    // thumbnail: made when the card is first drawn, dropped once its row is
    // well off screen (a list of 1000 Miis keeps a screenful or so). The
    // picture comes back from the renderer's cache when it returns.
    MiiPreview* thumbnail() const { return thumb.get(); }
    void dropThumbnail() { thumb.reset(); }
    std::function<void(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext*)> overlay;
    bool empty = false;
    bool checked = false; // chosen, while choosing several
    bool favorite = false; // a red star at the top left
    bool personal = false; // the 3DS's personal Mii: a gold crown there
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        if (!empty) {
            if (!thumb) {
                brls::View::NoXMLAttributes noXml; // (see rebuild)
                thumb.reset(new MiiPreview(mii, 160));
                thumb->setDimensions(width, height);
                HeadRenderer::instance().setPriority(thumb.get(), priority());
            }
            thumb->draw(vg, x, y, width, height, style, ctx);
        }
        if (empty) { // an empty place, or the held Mii's
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 3, y + 3, width - 6, height - 6, 16);
            nvgStrokeColor(vg, ui::rule());
            nvgStrokeWidth(vg, 3);
            nvgStroke(vg);
        }
        // the badges, top left: the crown, then the star
        float bx = x + 12;
        const float by = y + 12, bs = 34;
        if (personal) {
            static IconView* crown = new IconView("crown", bs, nvgRGB(0xE8, 0xB0, 0x10));
            crown->draw(vg, bx, by, bs, bs, style, ctx);
            bx += bs + 4;
        }
        if (favorite) {
            static IconView* star = new IconView("star", bs, nvgRGB(0xE5, 0x2E, 0x2E));
            star->draw(vg, bx, by, bs, bs, style, ctx);
        }
        if (checked) {
            // an accent outline and a ticked circle at the top right
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 2, y + 2, width - 4, height - 4, 17);
            nvgStrokeColor(vg, ui::accent());
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
            const float cx = x + width - 26, cy = y + 26;
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, 16);
            nvgFillColor(vg, ui::accent());
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 8, cy);
            nvgLineTo(vg, cx - 2, cy + 6);
            nvgLineTo(vg, cx + 8, cy - 6);
            nvgStrokeColor(vg, nvgRGB(255, 255, 255));
            nvgStrokeWidth(vg, 3.5f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        }
        if (overlay)
            overlay(vg, x, y, width, height, style, ctx);
    }
    // where its thumbnail comes in the renderer's queue: nearest the cursor
    // first
    int index = 0;
    inline static int cursorAt = 0;
    int priority() const
    {
        const int r = index / kColumns, c = index % kColumns, fr = cursorAt / kColumns, fc = cursorAt % kColumns;
        return 2 * std::abs(r - fr) + std::abs(c - fc);
    }

  private:
    const mii_charinfo_t* mii;
    std::unique_ptr<MiiPreview> thumb;
};

// A row of cards: skipped whole while off screen (one check for five
// cards: borealis draws boxes whether on screen or not); far off, its
// cards let their thumbnails go.
class MiiRow : public brls::Box
{
  public:
    MiiRow()
        : brls::Box(brls::Axis::ROW)
    {
    }
    void frame(brls::FrameContext* ctx) override
    {
        const float off = ui::offscreenDistance(this);
        if (off > 0.0f) {
            if (holdsThumbs && off > kKeepThumbs) {
                for (brls::View* c : getChildren())
                    static_cast<MiiCard*>(c)->dropThumbnail();
                holdsThumbs = false;
            }
            return;
        }
        holdsThumbs = true;
        brls::Box::frame(ctx);
    }

  private:
    static constexpr float kKeepThumbs = 1200.0f; // (about two screens)
    bool holdsThumbs = false;
};

brls::View* LibraryActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(20, 40, 20, 40);

    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginBottom(20);
    actions->setAlignItems(brls::AlignItems::CENTER);
    auto* add = new brls::Button();
    add->setText("library/add"_i18n);
    add->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    add->setWidth(320);
    add->setHeight(64);
    add->setFontSize(24);
    add->setMarginRight(20);
    add->registerClickAction([this](brls::View*) {
        if (busy() || !store().usable())
            return true; // (a tap while choosing a Mii or holding one; the 3DS list before its personal Mii)
        addPlace = -1;
        showAddMenu();
        return true;
    });
    actions->addView(add);
    createButton = add;
    topButtons.clear();
    topButtons.push_back(add);
    auto* send = new brls::Button();
    send->setText("library/send"_i18n);
    send->setWidth(260);
    send->setMarginRight(20);
    send->setHeight(64);
    send->setFontSize(24);
    send->registerClickAction([this](brls::View*) {
        if (busy() || !store().usable())
            return true;
        showSendMenu();
        return true;
    });
    actions->addView(send);
    topButtons.push_back(send);
    auto* receive = new brls::Button();
    receive->setText("library/receive"_i18n);
    receive->setWidth(260);
    receive->setHeight(64);
    receive->setFontSize(24);
    receive->registerClickAction([this](brls::View*) {
        if (busy() || !store().usable())
            return true;
        showReceiveMenu();
        return true;
    });
    actions->addView(receive);
    topButtons.push_back(receive);
    pickLabel = new brls::Label();
    pickLabel->setFontSize(22);
    pickLabel->setTextColor(ui::accent());
    pickLabel->setMarginLeft(30);
    pickLabel->setVisibility(brls::Visibility::GONE);
    actions->addView(pickLabel);
    // View options (sort / show), then settings (as START): at the right end
    auto* spacer = new brls::Box();
    spacer->setGrow(1);
    actions->addView(spacer);
    auto* view = new brls::Button();
    view->setText("");
    view->setDimensions(64, 64);
    view->setMarginRight(14);
    view->setPadding(0, 0, 0, 0);
    view->setJustifyContent(brls::JustifyContent::CENTER);
    view->setAlignItems(brls::AlignItems::CENTER);
    view->addView(new IconView("view_options", 40, ui::text()));
    view->registerClickAction([this](brls::View*) {
        if (busy())
            return true;
        showViewOptions();
        return true;
    });
    actions->addView(view);
    topButtons.push_back(view);
    viewButton = view;
    auto* cog = new brls::Button();
    cog->setText("");
    cog->setDimensions(64, 64);
    cog->setMarginRight(6); // (room for its shadow)
    cog->setPadding(0, 0, 0, 0);
    cog->setJustifyContent(brls::JustifyContent::CENTER);
    cog->setAlignItems(brls::AlignItems::CENTER);
    cog->addView(new IconView("settings", 40, ui::text()));
    cog->registerClickAction([this](brls::View*) {
        if (busy())
            return true;
        showSettings();
        return true;
    });
    actions->addView(cog);
    topButtons.push_back(cog);
    settingsButton = cog;
    content->addView(actions);

    // Tell the user where the 3D face resource goes (or that it's in use).
    auto* status = new brls::Label();
    status->setText(HeadRenderer::instance().status());
    status->setFontSize(16);
    status->setTextColor(ui::textDim());
    status->setMarginBottom(12);
    // debug information: SELECT shows it (with the render times)
    status->setVisibility(MiiPreview::showTimings ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    content->addView(status);

    empty = new brls::Label();
    empty->setText("library/empty"_i18n);
    empty->setFontSize(22);
    content->addView(empty);

    grid = new brls::Box(brls::Axis::COLUMN);
    content->addView(grid);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setContentView(content);

    auto* frame = new brls::AppletFrame(scroll);
    rootFrame = frame;
    appletFrame = frame;
    frame->setTitle(tabTitle(tab));
    // R / L: the next list (the 3DS's, the Wii's) / the one before
    tabActions();
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Q), [this](brls::View*) {
        switchTab(tab - 1);
        return true;
    });
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_E), [this](brls::View*) {
        switchTab(tab + 1);
        return true;
    });
    // how many Miis there are, at the top right
    countLabel = new brls::Label();
    countLabel->setFontSize(22);
    countLabel->setTextColor(ui::textDim());
    countLabel->setMarginTop(4);
    frame->getHeader()->setAlignItems(brls::AlignItems::CENTER);
    frame->getHeader()->addView(countLabel);
    // This is the root screen: "back" only leaves "choose a Mii" mode (popping
    // the last activity would leave borealis with input blocked). The app is
    // closed from the system (PS button), like other Vita apps.
    settingsAction(true);
    frame->registerAction("", brls::BUTTON_BACK, [status](brls::View*) {
        MiiPreview::showTimings = !MiiPreview::showTimings;
        status->setVisibility(MiiPreview::showTimings ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        return true;
    }, true);
    // desktop builds: P (keyboards have no START)
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_P), [this](brls::View*) {
        if (!holding && pick == Pick::NONE)
            showSettings();
        return true;
    });
    // desktop builds: T works as Triangle (pick up / swap)
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_T), [this](brls::View*) {
        brls::View* focus = brls::Application::getCurrentFocus();
        for (size_t i = 0; i < cards.size(); i++)
            if (cards[i] == focus) {
                if (holding)
                    swapHeld((int)i);
                else if (pick == Pick::NONE && store().canMove() && !arranged() && hasMii((int)i))
                    startHold((int)i);
            }
        return true;
    });
    frame->registerAction("", brls::BUTTON_B, [this](brls::View*) {
        if (pick != Pick::NONE)
            brls::sync([this] { setPick(Pick::NONE); });
        return true;
    }, true);
    return frame;
}

// START opens the settings; while a Mii is held or one is being chosen its
// hint is hidden and it does nothing. (Registered again in place, never
// unregistered: borealis numbers a new action after the last in its list,
// which can repeat the number of one replaced in place, like the frame's
// Circle, and unregistering by number then removed that one instead.)
void LibraryActivity::settingsAction(bool shown)
{
    settingsShown = shown;
    rootFrame->registerAction(shown ? "settings/title"_i18n : "", brls::BUTTON_START, [this](brls::View*) {
        if (!holding && pick == Pick::NONE)
            showSettings();
        return true;
    }, !shown);
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void LibraryActivity::onContentAvailable()
{
    rebuild();
}

// Some borealis views take their colors when created: build the screen again
// in the new theme. Focus moves to the new screen while the old one still
// exists, then they swap.
void LibraryActivity::rebuildForTheme()
{
    pick = Pick::NONE;
    brls::View* fresh = createContentView();
    brls::Application::giveFocus(createButton);
    setContentView(fresh);
    // (the old screen's cards are gone with it: all made again)
    cards.clear();
    builtTab = -1;
    focusedCard = -1;
    rebuild();
}

void LibraryActivity::openEditor(const mii_charinfo_t& start)
{
    // no cross-fade: the old screen should not linger behind the new one
    if (tab == TAB_3DS)
        Cfl3dsStore::instance().setNextPlace(addPlace);
    brls::Application::pushActivity(new EditorActivity(start, [this](const mii_charinfo_t& saved) {
        rebuild();
        // the cursor on the Mii just saved
        for (size_t i = 0; i < miis.size(); i++)
            if (present[i] && mii_create_id_equal(&miis[i].create_id, &saved.create_id)) {
                focusCard((int)i);
                break;
            }
    }, &store()),
        brls::TransitionAnimation::NONE);
}

// Cross on a Mii: its own screen. Back here, the cursor is on the Mii it
// showed last (L / R move through the list there).
void LibraryActivity::openView(int index)
{
    // the Miis there are (none of the empty places)
    std::vector<mii_charinfo_t> list;
    std::vector<int> at;
    int start = 0;
    for (size_t i = 0; i < miis.size(); i++)
        if (present[i]) {
            if ((int)i == index)
                start = (int)list.size();
            list.push_back(miis[i]);
            at.push_back(storeAt[i]);
        }
    if (list.empty())
        return;
    brls::Application::pushActivity(new MiiViewActivity(list, at, start, &store(), [this](int shown, bool changed) {
        if (changed)
            rebuild();
        // (`shown` is its index in the list: its card)
        for (size_t k = 0; k < storeAt.size(); k++)
            if (storeAt[k] == shown)
                focusCard((int)k);
    }),
        brls::TransitionAnimation::NONE);
}

// Up from the first row: the Add Mii button, except while choosing a Mii
// or holding one (the cursor stays in the list then).
// Choosing several (deleting, sending): Cross ticks a Mii or unticks it.
void LibraryActivity::toggleChosen(int index)
{
    if (!hasMii(index))
        return;
    if (chosen.size() != cards.size())
        chosen.assign(cards.size(), false);
    chosen[index] = !chosen[index];
    static_cast<MiiCard*>(cards[index])->checked = chosen[index];
    allCardActions(); // the count on START, Select / Unselect
}

int LibraryActivity::chosenCount() const
{
    int n = 0;
    for (bool c : chosen)
        n += c ? 1 : 0;
    return n;
}

// START while choosing several: delete them (after asking) or send them.
void LibraryActivity::finishChoosing()
{
    std::vector<int> picked;
    for (size_t i = 0; i < chosen.size(); i++)
        if (chosen[i])
            picked.push_back((int)i);
    if (picked.empty()) {
        brls::Application::notify("library/choose_first"_i18n);
        return;
    }
    if (pick == Pick::SEND) {
        std::vector<mii_charinfo_t> out;
        for (int i : picked)
            out.push_back(miis[i]);
        setPick(Pick::NONE);
        brls::Application::pushActivity(new TradeActivity(out), brls::TransitionAnimation::NONE);
        return;
    }
    auto* dialog = new brls::Dialog(picked.size() == 1 ? "library/delete_confirm"_i18n
                                                       : brls::getStr("library/delete_confirm_n", std::to_string(picked.size())));
    dialog->addButton("hints/cancel"_i18n, [] {});
    // (the cards' indices in the list, from the end: the others stay right)
    std::vector<int> inList;
    for (int i : picked)
        inList.push_back(storeAt[i]);
    std::sort(inList.begin(), inList.end());
    dialog->addButton("library/delete"_i18n, [this, picked, inList] {
        MiiStore& lib = store();
        bool ok = true, kept = false;
        lib.beginBatch(); // (written once, at the end)
        for (auto it = inList.rbegin(); it != inList.rend(); ++it) {
            if (!lib.canRemove(*it)) {
                kept = true; // the 3DS's personal Mii
                continue;
            }
            ok = lib.remove(*it) && ok;
        }
        ok = lib.endBatch() && ok;
        if (kept)
            brls::Application::notify("library/personal_kept"_i18n);
        else if (!ok)
            brls::Application::notify("library/delete_failed"_i18n);
        // after the dialog has closed and released focus; the cursor on
        // the Mii now where the first deleted one was
        const int at = picked.front();
        brls::sync([this, at] {
            pick = Pick::NONE;
            pickLabel->setVisibility(brls::Visibility::GONE);
            settingsButton->setVisibility(brls::Visibility::VISIBLE);
            viewButton->setVisibility(brls::Visibility::VISIBLE);
            chosen.clear();
            rebuild();
            if (!cards.empty())
                focusCard(std::min(at, (int)cards.size() - 1));
        });
    });
    dialog->open();
}

// The lists: the L / R hints name the one before / after
void LibraryActivity::tabActions()
{
    const bool hasNext = tab + 1 < TAB_COUNT, hasPrev = tab > 0;
    rootFrame->registerAction(hasNext ? tabName(tab + 1) : "", brls::BUTTON_RB, [this](brls::View*) {
        brls::sync([this] { switchTab(tab + 1); });
        return true;
    }, !hasNext);
    rootFrame->registerAction(hasPrev ? tabName(tab - 1) : "", brls::BUTTON_LB, [this](brls::View*) {
        brls::sync([this] { switchTab(tab - 1); });
        return true;
    }, !hasPrev);
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void LibraryActivity::clearNextPlace() { Cfl3dsStore::instance().setNextPlace(-1); }

MiiStore& LibraryActivity::store(int t)
{
    if (t == TAB_3DS)
        return Cfl3dsStore::instance();
    if (t == TAB_WII)
        return WiiStore::instance();
    return MiiLibrary::instance();
}

std::string LibraryActivity::tabName(int t)
{
    return t == TAB_3DS ? "library/tab_3ds"_i18n : t == TAB_WII ? "library/tab_wii"_i18n : "library/tab_vita"_i18n;
}

std::string LibraryActivity::tabTitle(int t)
{
    return t == TAB_3DS ? "library/title_3ds"_i18n : t == TAB_WII ? "library/title_wii"_i18n : "library/title"_i18n;
}

std::string LibraryActivity::fullMessage() const
{
    return tab == TAB_3DS ? "library/full_3ds"_i18n : tab == TAB_WII ? "library/full_wii"_i18n : "library/import_full"_i18n;
}

// The cursor on a card once the list is laid out (so it scrolls there)
void LibraryActivity::focusCard(int index)
{
    brls::sync([this, index] {
        if (index >= 0 && index < (int)cards.size())
            brls::Application::giveFocus(cards[index]);
    });
}

// R: the 3DS's Miis, read from Azahar's Mii database (when there is one
// this app can safely change), then the Wii's; L: back. A Mii held goes
// along, to be copied into the other list.
void LibraryActivity::switchTab(int to)
{
    if (to == tab || to < 0 || to >= TAB_COUNT || pick != Pick::NONE)
        return;
    // read again each time: Azahar may have changed the 3DS's meanwhile
    if (to == TAB_3DS) {
        Cfl3dsStore& s = Cfl3dsStore::instance();
        s.load();
        if (holding && !s.usable()) {
            brls::Application::notify(s.loaded() || s.error() == "missing" ? "library/need_personal"_i18n : "library/bad_3ds"_i18n);
            return;
        }
    } else if (to == TAB_WII) {
        WiiStore& w = WiiStore::instance();
        w.load();
        if (holding && !w.usable()) {
            brls::Application::notify("library/bad_wii"_i18n);
            return;
        }
    }
    tab = to;
    appletFrame->setTitle(tabTitle(tab));
    tabActions();
    const int at = heldAt;
    rebuild();
    if (holding && !cards.empty())
        focusCard(std::max(0, std::min(at, (int)cards.size() - 1)));
}

void LibraryActivity::upRoutes()
{
    const bool toButtons = !busy();
    for (int i = 0; i < std::min((int)cards.size(), kColumns); i++)
        cards[i]->setCustomNavigationRoute(brls::FocusDirection::UP, toButtons ? createButton : cards[i]);
}

void LibraryActivity::rebuild()
{
    MiiStore& lib = store();
    const bool usable = lib.usable();

    // one card per Mii; the 3DS's list: its 100 places. Carrying a Mii from
    // the 3DS, one empty card at the end of this list to put it there.
    // View options: sorted, or only some shown (then no empty places)
    storeAt.clear();
    if (usable) {
        if (!arranged()) {
            for (int i = 0; i < lib.places(); i++)
                storeAt.push_back(i);
        } else {
            const Settings& st = Settings::get();
            struct Shown {
                int at;
                mii_charinfo_t mii;
                std::string name;
                bool favorite;
            };
            std::vector<Shown> list;
            for (int i = 0; i < lib.places(); i++) {
                Shown s { i, {}, {}, false };
                if (!lib.get(i, &s.mii))
                    continue;
                s.favorite = lib.details(s.mii.create_id).favorite;
                if ((st.showMode == SHOW_FAVORITES && !s.favorite) || (st.showMode == SHOW_MALE && s.mii.gender != 0) ||
                    (st.showMode == SHOW_FEMALE && s.mii.gender != 1))
                    continue;
                char name[64];
                mii_name_get_utf8(&s.mii, name, sizeof(name));
                s.name = name;
                for (char& c : s.name)
                    c = (char)std::tolower((unsigned char)c);
                list.push_back(std::move(s));
            }
            auto byName = [](const Shown& a, const Shown& b) { return a.name < b.name; };
            if (st.sortMode == SORT_NAME)
                std::stable_sort(list.begin(), list.end(), byName);
            else if (st.sortMode == SORT_COLOR)
                std::stable_sort(list.begin(), list.end(), [&](const Shown& a, const Shown& b) {
                    return a.mii.favorite_color != b.mii.favorite_color ? a.mii.favorite_color < b.mii.favorite_color : byName(a, b);
                });
            else if (st.sortMode == SORT_HEIGHT)
                std::stable_sort(list.begin(), list.end(), [](const Shown& a, const Shown& b) { return a.mii.height < b.mii.height; });
            // Favorites First: on top of the order chosen
            if (st.favoritesFirst)
                std::stable_partition(list.begin(), list.end(), [](const Shown& s) { return s.favorite; });
            for (const Shown& s : list)
                storeAt.push_back(s.at);
        }
        if (copying() && !lib.fixedPlaces() && lib.count() < lib.capacity())
            storeAt.push_back(-1);
    }
    const int n = (int)storeAt.size();
    // As many cards as before in the same list (a Mii saved, edited,
    // looked at): the cards stay and show the Miis again, and the cursor
    // stays where it is; otherwise they are made anew.
    const bool reuse = n > 0 && n == (int)cards.size() && builtTab == tab;
    if (!reuse) {
        // The cards are about to be freed; never leave focus on one of them
        // (Add Mii takes it for now, even while it is off limits in a mode;
        // the mode puts the cursor back on a Mii below).
        createButton->setFocusable(true);
        brls::Application::giveFocus(createButton);
        grid->clearViews();
        cards.clear();
        focusedCard = -1;
    }
    builtTab = tab;
    miis.assign(n, mii_charinfo_t {}); // (previews keep pointers into it: same size, same place)
    present.assign(n, false);
    for (int i = 0; i < n; i++) {
        present[i] = storeAt[i] >= 0 && lib.get(storeAt[i], &miis[i]);
        if (!present[i])
            mii_charinfo_init_default(&miis[i], 0);
    }
    // the 3DS's list needs a personal Mii (made in Mii Maker) before
    // anything can be done there
    if (tab == TAB_3DS) {
        Cfl3dsStore& c3 = Cfl3dsStore::instance();
        empty->setText(c3.loaded() || c3.error() == "missing" ? "library/need_personal"_i18n : "library/bad_3ds"_i18n);
        empty->setVisibility(usable ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    } else if (!usable) {
        empty->setText("library/bad_wii"_i18n);
        empty->setVisibility(brls::Visibility::VISIBLE);
    } else {
        empty->setText("library/empty"_i18n);
        empty->setVisibility(lib.count() == 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (countLabel) {
        std::string count = brls::getStr("library/count", std::to_string(lib.count()), std::to_string(lib.capacity()));
        if (Settings::get().showMode != SHOW_ALL) // only some shown: how many
            count = brls::getStr("library/count_shown", std::to_string(std::count(present.begin(), present.end(), true))) + "   " + count;
        countLabel->setText(count);
        countLabel->setVisibility(usable ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    brls::Box* row = nullptr;
    held = holding && holdFromTab == tab ? holdFrom : -1;
    heldAt = -1;
    // while choosing a Mii, the cursor stays on the Miis; with no personal
    // Mii on the 3DS, only the settings work
    for (brls::View* b : topButtons) {
        const bool works = usable || b == settingsButton;
        b->setFocusable(pick == Pick::NONE && !holding && works);
        b->setAlpha(works ? 1.0f : 0.35f);
    }
    if (rootFrame)
        updateSettingsAction(); // in case a Mii was held, or the mode changed
    if (reuse) {
        for (int i = 0; i < n; i++) {
            auto* card = static_cast<MiiCard*>(cards[i]);
            card->empty = !present[i];
            card->favorite = present[i] && lib.details(miis[i].create_id).favorite;
            card->personal = present[i] && lib.isPersonal(storeAt[i]);
            card->checked = false;
            card->overlay = nullptr;
            card->setHideHighlight(false);
        }
        upRoutes();
        buttonRoutes();
        if (holding)
            holdLook();
        allCardActions();
        if ((pick != Pick::NONE || holding) && !cards.empty())
            brls::Application::giveFocus(cards[0]);
        else if (!usable && settingsButton)
            brls::Application::giveFocus(settingsButton);
        return;
    }
    cardActionIds.assign(miis.size(), {});
    // (rows and cards are made here in their hundreds and never given XML
    // attributes: made without them, they are made several times faster)
    auto noXml = std::make_unique<brls::View::NoXMLAttributes>();
    // Each row is filled before it joins the screen, and the rows join it
    // together at the end: borealis lays out the whole screen each time a
    // view is added to it (a card at a time, 100 cards were 120 layouts).
    std::vector<brls::Box*> rowsMade;
    for (size_t i = 0; i < miis.size(); i++) {
        if (i % kColumns == 0) {
            row = new MiiRow();
            row->setMarginBottom(20);
            // the 3DS's pages (ten Miis each) a little apart
            if (lib.fixedPlaces() && i && i % 10 == 0)
                row->setMarginTop(16);
            rowsMade.push_back(row);
        }
        auto* card = new MiiCard(&miis[i]);
        card->index = (int)i;
        cards.push_back(card);
        card->setDimensions(200, 250);
        card->setMarginRight(20);
        card->setFocusable(true);
        card->setCornerRadius(18);
        card->setHighlightCornerRadius(20);
        card->empty = !present[i];
        card->favorite = present[i] && lib.details(miis[i].create_id).favorite;
        card->personal = present[i] && lib.isPersonal(storeAt[i]);

        const int index = (int)i;
        card->registerClickAction([this, index](brls::View*) {
            if (holding) {
                endHold(true);
            } else if (!hasMii(index)) {
                // an empty place: a new Mii there
                if (pick == Pick::NONE) {
                    addPlace = storeAt[index];
                    showAddMenu();
                }
            } else if (pick == Pick::EXPORT) {
                exportMii(index); // stays in this mode: Circle ends it
            } else if (choosingSeveral()) {
                toggleChosen(index);
            } else if (pick == Pick::KID) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new ChildActivity(mii, &store(), [this] { rebuild(); }),
                    brls::TransitionAnimation::NONE);
            } else if (pick == Pick::PARENT1) {
                parentA = miis[index];
                parentAIndex = index;
                setPick(Pick::PARENT2);
            } else if (pick == Pick::PARENT2) {
                if (index == parentAIndex) {
                    brls::Application::notify("library/child_same"_i18n);
                    return true;
                }
                const mii_charinfo_t a = parentA, b = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new ChildActivity(a, b, &store(), [this] { rebuild(); }),
                    brls::TransitionAnimation::NONE);
            } else if (pick == Pick::QR) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new QrShowActivity(mii, &store()), brls::TransitionAnimation::NONE);
            } else {
                openView(index);
            }
            return true;
        });
        card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
        card->getFocusEvent()->subscribe([this, index](brls::View*) {
            focusedCard = index;
            cardActions(index); // (its buttons as things are now)
            thumbPriorities(index);
            if (holding)
                showHeldAt(index);
        });
        row->addView(card);
    }
    auto* sheet = new brls::Box(brls::Axis::COLUMN);
    for (brls::Box* r : rowsMade)
        sheet->addView(r);
    noXml.reset(); // (what follows makes views from XML: the hints)
    grid->addView(sheet);
    // up / down go to the card straight above or below (borealis would go
    // back to the last card focused in that row); a short last row takes
    // its last card
    const int count = (int)cards.size();
    for (int i = 0; i < count; i++) {
        if (i >= kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::UP, cards[i - kColumns]);
        if (i / kColumns < (count - 1) / kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, cards[std::min(i + kColumns, count - 1)]);
    }
    // up from the first row: the Add Mii button; down from the buttons: the
    // first Mii
    upRoutes();
    buttonRoutes();
    if (holding)
        holdLook(); // (a Mii carried over from the other list)
    allCardActions();
    thumbPriorities(0);
    if ((pick != Pick::NONE || holding) && !cards.empty())
        brls::Application::giveFocus(cards[0]);
    else if (!usable && settingsButton)
        brls::Application::giveFocus(settingsButton); // (the 3DS's list, before its personal Mii)
}

// Every card's buttons, then one hints update (one per card made picking a
// Mii up and putting it down stutter).
// The buttons of the card under the cursor for the current state (only it
// can use them: each card gets its own as the cursor comes to it; doing
// all of them at every change was up to 1000 cards' worth).
//
// Done once the input being handled is over: this often runs from one of
// that card's own actions (Cross on it), and borealis still reads the
// action after it returns: changing that card's actions under it crashed.
void LibraryActivity::allCardActions()
{
    if (actionsPending)
        return;
    actionsPending = true;
    brls::sync([this] {
        actionsPending = false;
        brls::View* focus = brls::Application::getCurrentFocus();
        if (focusedCard >= 0 && focusedCard < (int)cards.size() && cards[focusedCard] == focus)
            cardActions(focusedCard);
        else
            brls::Application::getGlobalHintsUpdateEvent()->fire();
    });
}

// START (settings) is there only in the plain list: not while a Mii is held
// or one is being chosen. Changed after this input is handled (the frame may
// be going through its actions right now).
void LibraryActivity::updateSettingsAction()
{
    brls::sync([this] {
        const bool want = !holding && pick == Pick::NONE;
        if (want != settingsShown)
            settingsAction(want);
    });
}

// Thumbnails nearest the cursor are drawn first.
void LibraryActivity::thumbPriorities(int focus)
{
    MiiCard::cursorAt = focus;
    // (only cards holding a thumbnail, a screenful or so: the others work
    // theirs out when they make one)
    for (brls::View* v : cards) {
        auto* card = static_cast<MiiCard*>(v);
        if (MiiPreview* t = card->thumbnail())
            HeadRenderer::instance().setPriority(t, card->priority());
    }
}

// The card's buttons for the current state (hints change with them).
void LibraryActivity::cardActions(int index)
{
    brls::View* card = cards[index];
    for (brls::ActionIdentifier id : cardActionIds[index])
        card->unregisterAction(id);
    cardActionIds[index].clear();
    auto& ids = cardActionIds[index];
    const bool here = hasMii(index);
    if (holding) {
        // carried from the other list: a copy goes here (on the 3DS, into
        // an empty place)
        card->updateActionHint(brls::BUTTON_A, !copying()                       ? "library/place"_i18n
                : here && store().fixedPlaces() ? std::string("")
                                                : "library/copy_here"_i18n);
        if (!copying())
            ids.push_back(card->registerAction(here ? "library/swap"_i18n : "library/place"_i18n, brls::BUTTON_Y,
                [this, index](brls::View*) {
                    swapHeld(index);
                    return true;
                }));
        ids.push_back(card->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](brls::View*) {
            endHold(false);
            return true;
        }));
    } else {
        const bool isChosen = index < (int)chosen.size() && chosen[index];
        card->updateActionHint(brls::BUTTON_A, !here ? (pick == Pick::NONE ? "library/add_here"_i18n : std::string(""))
                : pick == Pick::EXPORT                                      ? "library/export"_i18n
                : pick == Pick::QR                                          ? "library/qr"_i18n
                : choosingSeveral() ? (isChosen ? "library/unselect"_i18n : "library/select"_i18n)
                : pick == Pick::PARENT1 || pick == Pick::PARENT2 || pick == Pick::KID ? "library/child_choose"_i18n
                                                                                      : "library/view"_i18n);
        if (pick != Pick::NONE) {
            // choosing a Mii: Circle ends it (Done after exporting several);
            // no deleting or picking up meanwhile
            ids.push_back(card->registerAction(pick == Pick::EXPORT ? "library/pick_done"_i18n : "hints/cancel"_i18n,
                brls::BUTTON_B, [this](brls::View*) {
                    // after this action: leaving the mode replaces it
                    brls::sync([this] { setPick(Pick::NONE); });
                    return true;
                }));
            if (choosingSeveral()) {
                // START: on with the chosen Miis
                const int n = chosenCount();
                ids.push_back(card->registerAction(
                    brls::getStr(pick == Pick::DELETE ? "library/delete_n" : "library/send_n", std::to_string(n)),
                    brls::BUTTON_START, [this](brls::View*) {
                        brls::sync([this] { finishChoosing(); });
                        return true;
                    }));
            }
            if (!batchingHints)
                brls::Application::getGlobalHintsUpdateEvent()->fire();
            return;
        }
        if (here) {
            // Square: choose Miis to delete, starting with this one
            ids.push_back(card->registerAction("library/delete"_i18n, brls::BUTTON_X, [this, index](brls::View*) {
                brls::sync([this, index] {
                    setPick(Pick::DELETE);
                    toggleChosen(index);
                });
                return true;
            }));
            if (store().canMove() && !arranged()) // (sorted: there is no order to change)
                ids.push_back(card->registerAction("library/pick_up"_i18n, brls::BUTTON_Y, [this, index](brls::View*) {
                    startHold(index);
                    return true;
                }));
        }
    }
    if (!batchingHints)
        brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void LibraryActivity::startHold(int index)
{
    holding = true;
    holdFromTab = tab;
    holdFrom = held = index;
    heldMii = miis[index];
    holdLook();
    showHeldAt(index);
    allCardActions();
}

// How the list looks while a Mii is held: its place empty (when it is from
// this list), no highlight under the lifted card, no settings, and the
// cursor kept in the list.
void LibraryActivity::holdLook()
{
    if (!floating) {
        floating.reset(new MiiPreview(&heldMii, 160));
        floating->setDimensions(200, 250);
    }
    if (held >= 0 && held < (int)cards.size()) {
        auto* card = static_cast<MiiCard*>(cards[held]);
        card->empty = true;
        card->favorite = card->personal = false;
    }
    for (brls::View* c : cards)
        c->setHideHighlight(true);
    updateSettingsAction();
    for (brls::View* b : topButtons)
        b->setFocusable(false);
    upRoutes();
}

// The held Mii floats above the card under the cursor, shifted up and left
// so that card's Mii shows too; nothing else moves until it is put down.
void LibraryActivity::showHeldAt(int index)
{
    heldAt = index;
    for (size_t i = 0; i < cards.size(); i++) {
        auto* card = static_cast<MiiCard*>(cards[i]);
        if ((int)i != index) {
            card->overlay = nullptr;
            continue;
        }
        card->overlay = [this](NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
            const float dx = -26, dy = -30;
            // a soft shadow under the lifted card
            NVGpaint shadow = nvgBoxGradient(vg, x + dx + 6, y + dy + 10, w, h, 18, 16, nvgRGBA(0, 0, 0, 90), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x + dx - 20, y + dy - 20, w + 52, h + 56);
            nvgFillPaint(vg, shadow);
            nvgFill(vg);
            floating->draw(vg, x + dx, y + dy, w, h, style, ctx);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + dx + 1.5f, y + dy + 1.5f, w - 3, h - 3, 18);
            nvgStrokeColor(vg, ui::accent());
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
        };
    }
}

// Rereads the Miis from the list into the cards (their previews point
// into `miis`), without rebuilding the screen; the held Mii's place stays
// empty.
void LibraryActivity::refreshCards()
{
    MiiStore& lib = store();
    for (size_t i = 0; i < miis.size(); i++) {
        present[i] = storeAt[i] >= 0 && lib.get(storeAt[i], &miis[i]);
        const bool shown = present[i] && (int)i != held;
        auto* card = static_cast<MiiCard*>(cards[i]);
        card->empty = !shown;
        card->favorite = shown && lib.details(miis[i].create_id).favorite;
        card->personal = shown && lib.isPersonal(storeAt[i]);
    }
}

void LibraryActivity::endHold(bool place)
{
    const int from = held, to = heldAt;
    // carried from the other list: a copy of it here (it stays held when
    // it cannot go there)
    if (place && copying()) {
        if (to < 0 || !copyHeld(to))
            return;
        holding = false;
        held = heldAt = -1;
        for (brls::View* c : cards)
            static_cast<MiiCard*>(c)->overlay = nullptr;
        rebuild();
        focusCard(to);
        return;
    }
    holding = false;
    held = heldAt = -1;
    updateSettingsAction();
    for (brls::View* b : topButtons)
        b->setFocusable(pick == Pick::NONE && (store().usable() || b == settingsButton));
    upRoutes();
    for (brls::View* c : cards) {
        static_cast<MiiCard*>(c)->overlay = nullptr;
        c->setHideHighlight(false);
    }
    // Put down on another card: it moves there and the Mii between close
    // up, taking the place it left (on the 3DS: into that place, or
    // trading places with the Mii there).
    if (place && from >= 0 && from != to && to >= 0) {
        if (!store().move(from, to))
            brls::Application::notify("library/move_failed"_i18n);
    }
    refreshCards();
    allCardActions();
}

// The held Mii, from the other list, copied in at `to` with its details
// (favorite, birthday, creator); a copy of its own (a new create id).
bool LibraryActivity::copyHeld(int to)
{
    MiiStore& into = store();
    MiiStore& from = store(holdFromTab);
    if (into.fixedPlaces() && hasMii(to)) {
        brls::Application::notify("library/place_taken"_i18n);
        return false;
    }
    if (into.count() >= into.capacity()) {
        brls::Application::notify(fullMessage());
        return false;
    }
    mii_charinfo_t m = heldMii;
    const MiiDetails d = from.details(m.create_id);
    MiiLibrary::instance().newCreateId(&m.create_id);
    into.snap(&m); // (the 3DS's or Wii's parts and colors)
    into.beginBatch(); // (the Mii and its details: written once)
    const bool put = into.putAt(&m, to) >= 0;
    if (put)
        into.setDetails(m.create_id, d);
    if (!into.endBatch() || !put) {
        brls::Application::notify("library/copy_failed"_i18n);
        return false;
    }
    char name[64];
    mii_name_get_utf8(&m, name, sizeof(name));
    brls::Application::notify(brls::getStr("library/copied", std::string(name), tabName(tab)));
    return true;
}

// Puts the held Mii where the hovered one is and picks that one up (it
// now belongs in the empty place the held one left). On an empty place
// of the 3DS's: just put down there.
void LibraryActivity::swapHeld(int index)
{
    if (index == held || copying())
        return;
    if (!hasMii(index)) {
        heldAt = index;
        endHold(true);
        return;
    }
    if (!store().swap(held, index)) {
        brls::Application::notify("library/move_failed"_i18n);
        return;
    }
    refreshCards();
    heldMii = miis[held];
    showHeldAt(index);
}

// Import from SD Card: the Miis found there on a screen of their own, to
// choose which join the list.
void LibraryActivity::importMiis()
{
    MiiLibrary& lib = MiiLibrary::instance();
    int unreadable = 0;
    std::vector<MiiLibrary::Pending> found = lib.scanImports(&unreadable);
    if (found.empty()) {
        brls::Application::notify(unreadable ? std::to_string(unreadable) + " " + "library/import_failed"_i18n
                                             : "library/import_none"_i18n + " " + lib.importDir() + "/");
        return;
    }
    brls::Application::pushActivity(new ImportActivity(std::move(found), unreadable, &store(), [this](int imported) {
        if (imported)
            rebuild();
    }),
        brls::TransitionAnimation::NONE);
}

void LibraryActivity::exportMii(int index)
{
    std::string path;
    mii_ver3_info_t info;
    // a 3DS Mii as it is (its console, birthday...)
    const bool has3ds = tab == TAB_3DS && Cfl3dsStore::instance().info(miis[index].create_id, &info);
    if (MiiLibrary::instance().exportMii(miis[index], &path, has3ds ? &info : nullptr))
        brls::Application::notify("library/exported"_i18n + " " + path);
    else
        brls::Application::notify("library/export_failed"_i18n);
}

// A Mii read from a QR code joins the library (QR codes carry no create id,
// so it gets a new one).
void LibraryActivity::addScanned(const mii_charinfo_t& scanned, const mii_ver3_info_t& info)
{
    mii_charinfo_t mii = scanned;
    MiiLibrary& lib = MiiLibrary::instance();
    if (!mii_create_id_is_valid(&mii.create_id))
        lib.newCreateId(&mii.create_id);
    char name[64];
    mii_name_get_utf8(&mii, name, sizeof(name));
    store().snap(&mii);
    // the Mii and the profile its code carried (favorite, birthday,
    // creator...): written once
    store().beginBatch();
    const bool put = store().put(&mii) >= 0;
    if (put)
        store().setDetails(mii.create_id, MiiLibrary::detailsOf(info));
    if (!store().endBatch() || !put) {
        brls::Application::notify(fullMessage());
        return;
    }
    brls::Application::notify("qr/added"_i18n + " " + name);
    brls::sync([this] { rebuild(); });
}

// The 3DS's and Wii's lists: copies of the favorite Miis of the app's own
// list (made of that list's parts and colors), but none already there.
void LibraryActivity::importFavorites()
{
    MiiLibrary& lib = MiiLibrary::instance();
    MiiStore& into = store();
    // the same Mii: every part, color and the name (a copy has a create
    // id of its own)
    auto same = [](const mii_charinfo_t& a, const mii_charinfo_t& b) {
        for (int f = 0; f < MII_FIELD_COUNT; f++)
            if (mii_field_get(&a, (mii_field_t)f) != mii_field_get(&b, (mii_field_t)f))
                return false;
        return std::memcmp(a.name, b.name, sizeof(a.name)) == 0;
    };
    std::vector<mii_charinfo_t> there;
    for (int i = 0; i < into.places(); i++) {
        mii_charinfo_t m;
        if (into.get(i, &m))
            there.push_back(m);
    }
    int added = 0, already = 0, favorites = 0;
    bool full = false;
    into.beginBatch(); // (written once, at the end)
    for (int i = 0; i < lib.count(); i++) {
        mii_charinfo_t m;
        if (!lib.get(i, &m))
            continue;
        const MiiDetails d = lib.details(m.create_id);
        if (!d.favorite)
            continue;
        favorites++;
        into.snap(&m);
        bool dup = false;
        for (const mii_charinfo_t& t : there)
            if (same(t, m))
                dup = true;
        if (dup) {
            already++;
            continue;
        }
        if (into.count() >= into.capacity()) {
            full = true;
            break;
        }
        lib.newCreateId(&m.create_id);
        if (into.put(&m) < 0) {
            full = true;
            break;
        }
        into.setDetails(m.create_id, d);
        there.push_back(m);
        added++;
    }
    if (!into.endBatch()) {
        brls::Application::notify("library/copy_failed"_i18n);
        brls::sync([this] { rebuild(); });
        return;
    }
    std::string msg = !favorites   ? "library/no_favorites"_i18n
        : !added && already ? "library/favorites_all_here"_i18n
                            : brls::getStr("library/favorites_added", std::to_string(added));
    if (added && already)
        msg += " " + brls::getStr("library/favorites_already", std::to_string(already));
    if (full)
        msg += " " + fullMessage();
    brls::Application::notify(msg);
    if (added)
        brls::sync([this] { rebuild(); });
}

// The second parent's prompt names the first.
std::string LibraryActivity::parent2Prompt() const
{
    char name[64];
    mii_name_get_utf8(&parentA, name, sizeof(name));
    return brls::getStr("library/pick_parent2", std::string(name));
}

// "Choose a Mii" mode: the next card pressed is exported instead of edited.
void LibraryActivity::setPick(Pick mode)
{
    const bool wasChoosing = pick != Pick::NONE;
    pick = mode;
    // choosing several: none chosen yet
    chosen.assign(cards.size(), false);
    for (brls::View* c : cards)
        static_cast<MiiCard*>(c)->checked = false;
    pickLabel->setText(mode == Pick::EXPORT ? "library/pick_export"_i18n
            : mode == Pick::DELETE          ? "library/pick_delete"_i18n
            : mode == Pick::QR              ? "library/pick_qr"_i18n
            : mode == Pick::SEND            ? "library/pick_send"_i18n
            : mode == Pick::KID             ? "library/pick_kid"_i18n
            : mode == Pick::PARENT1         ? "library/pick_parent1"_i18n
            : mode == Pick::PARENT2         ? parent2Prompt()
                                            : "");
    pickLabel->setVisibility(mode == Pick::NONE ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    // (no settings while choosing a Mii; the prompt takes its place)
    // (hidden in its place: taken out of the layout and back, its icon came
    // back off center)
    settingsButton->setVisibility(mode == Pick::NONE ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
    viewButton->setVisibility(mode == Pick::NONE ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
    // The cards stay as they are (this often runs from a card's own action,
    // which must not delete that card): only their buttons, the top
    // buttons and the routes change.
    for (brls::View* b : topButtons)
        b->setFocusable(mode == Pick::NONE);
    buttonRoutes();
    upRoutes();
    updateSettingsAction();
    allCardActions();
    // choosing a Mii starts on the first one
    // (unless the cursor is on a Mii already: Square starts on that one)
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool onCard = std::find(cards.begin(), cards.end(), focus) != cards.end();
    if (mode != Pick::NONE && !wasChoosing && !onCard)
        for (size_t i = 0; i < cards.size(); i++)
            if (present[i]) {
                brls::Application::giveFocus(cards[i]);
                break;
            }
}

// Down from the top buttons: the first Mii.
void LibraryActivity::buttonRoutes()
{
    for (brls::View* b : topButtons) {
        if (!b->isFocusable())
            continue; // (borealis refuses routes on those)
        b->setCustomNavigationRoute(brls::FocusDirection::DOWN, cards.empty() ? nullptr : cards[0]);
    }
}

// Mii QR codes need the user's own copy of the key (like the .dat files).
static void needKey()
{
    auto* dialog = new brls::Dialog("qr/need_key"_i18n + "\n\n" + qr::keyPath());
    dialog->addButton("hints/ok"_i18n, [] {});
    dialog->open();
}

void LibraryActivity::showAddMenu()
{
    std::vector<PopupMenu::Item> items = {
        PopupMenu::item("library/add_scratch"_i18n, [this] {
            brls::Application::pushActivity(new StartActivity([this](const mii_charinfo_t& ci) { openEditor(ci); }, false, store().limited() ? &store() : nullptr),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_lookalike"_i18n, [this] {
            brls::Application::pushActivity(new StartActivity([this](const mii_charinfo_t& ci) { openEditor(ci); }, true, store().limited() ? &store() : nullptr),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_kid"_i18n, [this] {
            if (store().count() == 0) {
                brls::Application::notify("library/empty"_i18n);
                return;
            }
            setPick(Pick::KID);
        }),
        PopupMenu::item("library/add_child"_i18n, [this] {
            if (store().count() < 2) {
                brls::Application::notify("library/child_need_two"_i18n);
                return;
            }
            parentAIndex = -1;
            setPick(Pick::PARENT1);
        }),
    };
    if (addPlace >= 0)
        items.resize(2); // (an empty place: a new Mii; the generators start from a Mii)
    else if (tab != TAB_VITA)
        items.push_back(PopupMenu::item("library/add_favorites"_i18n, [this] { importFavorites(); }));
    brls::Application::pushActivity(new PopupMenu("library/add"_i18n, items), brls::TransitionAnimation::NONE);
}

void LibraryActivity::showReceiveMenu()
{
    std::vector<PopupMenu::Item> items = {
        PopupMenu::item("library/add_import"_i18n, [this] { importMiis(); }),
        PopupMenu::item("library/add_qr"_i18n, [this] {
            if (!qr::available()) {
                needKey();
                return;
            }
            brls::Application::pushActivity(new QrScanActivity([this](const mii_charinfo_t& mii, const mii_ver3_info_t& info) { addScanned(mii, info); }),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_receive"_i18n, [this] {
            brls::Application::pushActivity(new TradeActivity(&store(), [this] { stale = true; }),
                brls::TransitionAnimation::NONE);
        }),
    };
    brls::Application::pushActivity(new PopupMenu("library/receive"_i18n, items), brls::TransitionAnimation::NONE);
}

void LibraryActivity::showSendMenu()
{
    if (store().count() == 0) {
        brls::Application::notify("library/empty"_i18n);
        return;
    }
    std::vector<PopupMenu::Item> items = {
        PopupMenu::item("library/send_export"_i18n, [this] { setPick(Pick::EXPORT); }),
        PopupMenu::item("library/send_qr"_i18n, [this] {
            if (!qr::available()) {
                needKey();
                return;
            }
            setPick(Pick::QR);
        }),
        PopupMenu::item("library/send_vita"_i18n, [this] { setPick(Pick::SEND); }),
    };
    brls::Application::pushActivity(new PopupMenu("library/send"_i18n, items), brls::TransitionAnimation::NONE);
}

// Settings > Default Skin Tone: the ten skin colors in a small panel over
// the list; picking one keeps the panel open (Circle closes it).
class SkinTonePicker : public brls::Activity
{
  public:
    brls::View* createContentView() override
    {
        mii_charinfo_init_default(&mii, 0);
        mii.faceline_color = Settings::get().skinTone;
        auto* root = new brls::Box();
        root->setJustifyContent(brls::JustifyContent::CENTER);
        root->setAlignItems(brls::AlignItems::CENTER);
        root->setBackgroundColor(nvgRGBA(0, 0, 0, 140));
        auto* panel = new brls::Box(brls::Axis::COLUMN);
        panel->setPadding(24, 30, 30, 30);
        panel->setBackgroundColor(ui::background());
        panel->setCornerRadius(8);
        panel->setAlignItems(brls::AlignItems::CENTER);
        auto* title = new brls::Label();
        title->setText("settings/skin"_i18n);
        title->setFontSize(28);
        title->setMarginBottom(18);
        panel->addView(title);
        int cols;
        const mii_grid_t* g = mii_editor_grid(MII_GRID_FACELINE_COLOR);
        cols = g->columns;
        OptionGrid::Options o;
        o.columns = cols;
        o.cell = 72;
        o.colorOf = [](uint8_t v) { return mii_faceline_color_rgb(v); };
        panel->addView(new OptionGrid(&mii, MII_FIELD_FACELINE_COLOR, std::vector<uint8_t>(g->items, g->items + g->count), o,
            [this] {
                Settings& s = Settings::get();
                s.skinTone = mii.faceline_color;
                s.save();
            }));
        root->addView(panel);
        root->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        });
        return root;
    }
    bool isTranslucent() override { return true; }

  private:
    mii_charinfo_t mii;
};

// A choice from a list (with the current one ticked), as the Switch's
// settings ask: `done` runs after the list has closed.
static void choose(const std::string& title, std::vector<std::string> names, int current, std::function<void(int)> picked,
    std::function<void(int)> done = nullptr)
{
    auto* dropdown = new brls::Dropdown(title, std::move(names), [picked](int i) {
        if (picked)
            picked(i);
    }, current, [done](int i) {
        if (done)
            done(i);
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryActivity::showSettings(int focus)
{
    static const char* const kThemes[] = { "settings/light", "settings/dark", "settings/dark2" };
    static const char* const kTracks[] = { "settings/music_none", "settings/music_wii", "settings/music_3ds",
                                           "settings/music_wiiu" };
    auto themeIndex = [] {
        const ui::Theme t = Settings::get().theme;
        return t == ui::Theme::DARK ? 1 : t == ui::Theme::DARK_BLUE ? 2 : 0;
    };

    PopupMenu::Item skin = PopupMenu::item("settings/skin"_i18n, [] {
        brls::Application::pushActivity(new SkinTonePicker(), brls::TransitionAnimation::NONE);
    });
    skin.keepOpen = true;
    skin.swatch = [] { return mii_faceline_color_rgb(Settings::get().skinTone); };

    PopupMenu::Item theme = PopupMenu::item("settings/theme"_i18n, nullptr);
    theme.keepOpen = true;
    theme.value = [themeIndex] { return brls::getStr(kThemes[themeIndex()]); };
    theme.action = [this, themeIndex] {
        auto chosen = std::make_shared<int>(-1);
        choose("settings/theme"_i18n, { "settings/light"_i18n, "settings/dark"_i18n, "settings/dark2"_i18n }, themeIndex(),
            [chosen](int i) { *chosen = i; },
            [this, chosen, themeIndex](int) {
                if (*chosen < 0 || *chosen == themeIndex())
                    return;
                // the whole screen, and this menu, in the new theme
                Settings& s = Settings::get();
                s.theme = *chosen == 1 ? ui::Theme::DARK : *chosen == 2 ? ui::Theme::DARK_BLUE : ui::Theme::LIGHT;
                s.save();
                brls::Application::popActivity(brls::TransitionAnimation::NONE, [this] {
                    ui::applyTheme(Settings::get().theme);
                    ui::applyVariant();
                    rebuildForTheme();
                    showSettings(1);
                });
            });
    };

    PopupMenu::Item musicItem = PopupMenu::item("settings/music"_i18n, nullptr);
    musicItem.keepOpen = true;
    musicItem.value = [] { return brls::getStr(kTracks[Settings::get().music]); };
    musicItem.action = [] {
        std::vector<std::string> names;
        for (const char* k : kTracks)
            names.push_back(brls::getStr(k));
        choose("settings/music"_i18n, names, Settings::get().music, [](int i) {
            Settings& s = Settings::get();
            if (i < 0 || i >= (int)music::Track::COUNT || i == s.music)
                return;
            s.music = i;
            s.save();
            std::string missing;
            if (!music::Player::instance().play((music::Track)s.music, &missing))
                brls::Application::notify("settings/music_missing"_i18n + " " + missing);
        });
    };

    PopupMenu::Item qrFormat = PopupMenu::item("settings/qr"_i18n, nullptr);
    qrFormat.keepOpen = true;
    qrFormat.value = [] { return Settings::get().qrSwitch2 ? "settings/qr_switch2"_i18n : "settings/qr_3ds"_i18n; };
    qrFormat.action = [] {
        choose("settings/qr"_i18n, { "settings/qr_switch2"_i18n, "settings/qr_3ds"_i18n }, Settings::get().qrSwitch2 ? 0 : 1,
            [](int i) {
                if (i < 0)
                    return;
                Settings& s = Settings::get();
                s.qrSwitch2 = i == 0;
                s.save();
            });
    };

    // Sound Effects (off, quiet...) and Music Volume
    static const char* const kLevels[4] = { "settings/off", "settings/quiet", "settings/medium", "settings/loud" };
    PopupMenu::Item sounds = PopupMenu::item("settings/sounds"_i18n, nullptr);
    sounds.keepOpen = true;
    sounds.value = [] { return brls::getStr(kLevels[Settings::get().soundLevel]); };
    sounds.off = [] { return Settings::get().soundLevel == 0; };
    sounds.action = [] {
        choose("settings/sounds"_i18n, { brls::getStr(kLevels[0]), brls::getStr(kLevels[1]), brls::getStr(kLevels[2]), brls::getStr(kLevels[3]) },
            Settings::get().soundLevel, [](int i) {
                if (i < 0)
                    return;
                Settings& s = Settings::get();
                s.soundLevel = i;
                s.save();
                sound::setLevel(i);
            });
    };
    PopupMenu::Item musicVolume = PopupMenu::item("settings/music_volume"_i18n, nullptr);
    musicVolume.keepOpen = true;
    musicVolume.value = [] { return brls::getStr(kLevels[Settings::get().musicLevel]); };
    musicVolume.action = [] {
        choose("settings/music_volume"_i18n, { brls::getStr(kLevels[1]), brls::getStr(kLevels[2]), brls::getStr(kLevels[3]) },
            Settings::get().musicLevel - 1, [](int i) {
                if (i < 0)
                    return;
                Settings& s = Settings::get();
                s.musicLevel = i + 1;
                s.save();
                music::Player::instance().setLevel(s.musicLevel);
            });
    };

    // a toggle: no list, pressing it switches it
    PopupMenu::Item curated = PopupMenu::item("settings/curated"_i18n, [] {
        Settings& s = Settings::get();
        s.curatedLookalikes = !s.curatedLookalikes;
        s.save();
    });
    curated.keepOpen = true;
    curated.value = [] { return Settings::get().curatedLookalikes ? "settings/on"_i18n : "settings/off"_i18n; };
    curated.off = [] { return !Settings::get().curatedLookalikes; };

    // Reset Cache: deletes the drawn icons and thumbnails (on the memory card
    // too); they are drawn again as they are shown
    PopupMenu::Item resetCache = PopupMenu::item("settings/reset_cache"_i18n, [this] {
        brls::sync([this] {
            auto* dialog = new brls::Dialog("settings/reset_cache_confirm"_i18n);
            dialog->addButton("hints/cancel"_i18n, [] {});
            dialog->addButton("settings/reset_cache"_i18n, [this] {
                HeadRenderer::instance().clearCache();
                brls::sync([this] {
                    builtTab = -1; // (every card made again: thumbnails drawn anew)
                    rebuild();
                    brls::Application::notify("settings/reset_cache_done"_i18n);
                });
            });
            dialog->open();
        });
    });
    brls::Application::pushActivity(
        new PopupMenu("settings/title"_i18n, { skin, theme, musicItem, musicVolume, sounds, qrFormat, curated, resetCache }, focus),
        brls::TransitionAnimation::NONE);
}

bool LibraryActivity::arranged()
{
    const Settings& s = Settings::get();
    return s.sortMode != SORT_OWN || s.showMode != SHOW_ALL || s.favoritesFirst;
}

// View Options: the order the Miis are shown in, and which are shown. The
// lists stay as they are kept (their own order is the first choice).
void LibraryActivity::showViewOptions()
{
    static const char* const kSorts[SORT_COUNT] = { "view_options/sort_own", "view_options/sort_name",
        "view_options/sort_color", "view_options/sort_height" };
    static const char* const kShows[SHOW_COUNT] = { "view_options/show_all", "view_options/show_favorites",
        "view_options/show_male", "view_options/show_female" };
    auto pickFrom = [this](const std::string& title, const char* const* names, int count, int* value) {
        std::vector<std::string> shown;
        for (int i = 0; i < count; i++)
            shown.push_back(brls::getStr(names[i]));
        choose(title, shown, *value, [this, value](int i) {
            if (i < 0 || i == *value)
                return;
            *value = i;
            Settings::get().save();
            stale = true; // (shown so once the menu closes)
        });
    };
    PopupMenu::Item sort = PopupMenu::item("view_options/sort"_i18n, nullptr);
    sort.keepOpen = true;
    sort.value = [] { return brls::getStr(kSorts[Settings::get().sortMode]); };
    sort.action = [pickFrom] { pickFrom("view_options/sort"_i18n, kSorts, SORT_COUNT, &Settings::get().sortMode); };
    PopupMenu::Item show = PopupMenu::item("view_options/show"_i18n, nullptr);
    show.keepOpen = true;
    show.value = [] { return brls::getStr(kShows[Settings::get().showMode]); };
    show.action = [pickFrom] { pickFrom("view_options/show"_i18n, kShows, SHOW_COUNT, &Settings::get().showMode); };
    // a switch: pressing it turns it on or off
    PopupMenu::Item favorites = PopupMenu::item("view_options/sort_favorites"_i18n, [this] {
        Settings& s = Settings::get();
        s.favoritesFirst = !s.favoritesFirst;
        s.save();
        stale = true; // (shown so once the menu closes)
    });
    favorites.keepOpen = true;
    favorites.value = [] { return Settings::get().favoritesFirst ? "settings/on"_i18n : "settings/off"_i18n; };
    favorites.off = [] { return !Settings::get().favoritesFirst; };
    brls::Application::pushActivity(new PopupMenu("view_options/title"_i18n, { sort, favorites, show }),
        brls::TransitionAnimation::NONE);
}
