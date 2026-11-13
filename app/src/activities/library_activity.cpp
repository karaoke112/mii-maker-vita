#include "activities/library_activity.hpp"

#include "activities/child_activity.hpp"
#include "activities/editor_activity.hpp"
#include "activities/qr_activity.hpp"
#include "activities/trade_activity.hpp"
#include "activities/start_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "music.hpp"
#include "mii_qr.hpp"
#include "settings.hpp"
#include "ui_theme.hpp"
#include "views/popup_menu.hpp"
#include <mii/mii_editor.h>
#include "views/controls.hpp"
#include "views/mii_preview.hpp"

using namespace brls::literals;

static constexpr int kColumns = 5;

// A Mii's card; while a Mii is held, the card under the cursor draws it
// lifted above itself.
class MiiCard : public brls::Box
{
  public:
    MiiCard()
        : brls::Box(brls::Axis::COLUMN)
    {
    }
    std::function<void(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext*)> overlay;
    bool empty = false;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        if (empty) { // the held Mii's place
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 3, y + 3, width - 6, height - 6, 16);
            nvgStrokeColor(vg, ui::rule());
            nvgStrokeWidth(vg, 3);
            nvgStroke(vg);
        }
        if (overlay)
            overlay(vg, x, y, width, height, style, ctx);
    }
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
    frame->setTitle("library/title"_i18n);
    rootFrame = frame;
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
        if (held < 0 && pick == Pick::NONE)
            showSettings();
        return true;
    });
    // desktop builds: T works as Triangle (pick up / swap)
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_T), [this](brls::View*) {
        brls::View* focus = brls::Application::getCurrentFocus();
        for (size_t i = 0; i < cards.size(); i++)
            if (cards[i] == focus) {
                if (held >= 0)
                    swapHeld((int)i);
                else if (pick == Pick::NONE)
                    startHold((int)i);
            }
        return true;
    });
    frame->registerAction("", brls::BUTTON_B, [this](brls::View*) {
        if (pick != Pick::NONE)
            setPick(Pick::NONE);
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
        if (held < 0 && pick == Pick::NONE)
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
    rebuild();
}

void LibraryActivity::openEditor(const mii_charinfo_t& start)
{
    // no cross-fade: the old screen should not linger behind the new one
    brls::Application::pushActivity(new EditorActivity(start, [this](const mii_charinfo_t&) { rebuild(); }),
        brls::TransitionAnimation::NONE);
}

void LibraryActivity::rebuild()
{
    MiiLibrary& lib = MiiLibrary::instance();

    // The cards are about to be freed; never leave focus on one of them.
    brls::Application::giveFocus(createButton);
    grid->clearViews();
    cards.clear();
    thumbs.clear();
    miis.clear();
    miis.reserve(MII_NFDB_MAX_MIIS); // previews keep pointers into this vector
    for (int i = 0; i < lib.count(); i++) {
        mii_charinfo_t ci;
        if (lib.get(i, &ci))
            miis.push_back(ci);
    }
    empty->setVisibility(miis.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (countLabel)
        countLabel->setText(brls::getStr("library/count", std::to_string(miis.size())));

    brls::Box* row = nullptr;
    held = heldAt = -1;
    for (brls::View* b : topButtons)
        b->setFocusable(true);
    if (rootFrame)
        updateSettingsAction(); // in case a Mii was held, or the mode changed
    cardActionIds.assign(miis.size(), {});
    for (size_t i = 0; i < miis.size(); i++) {
        if (i % kColumns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(20);
            grid->addView(row);
        }
        auto* card = new MiiCard();
        cards.push_back(card);
        card->setDimensions(200, 250);
        card->setMarginRight(20);
        card->setFocusable(true);
        card->setCornerRadius(18);
        card->setHighlightCornerRadius(20);
        auto* preview = new MiiPreview(&miis[i], 160);
        thumbs.push_back(preview);
        preview->setDimensions(200, 250);
        card->addView(preview);

        const int index = (int)i;
        card->registerClickAction([this, index](brls::View*) {
            if (held >= 0) {
                endHold(true);
            } else if (pick == Pick::EXPORT) {
                exportMii(index); // stays in this mode: Circle ends it
            } else if (pick == Pick::SEND) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new TradeActivity(mii), brls::TransitionAnimation::NONE);
            } else if (pick == Pick::KID) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new ChildActivity(mii, [this] { rebuild(); }), brls::TransitionAnimation::NONE);
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
                brls::Application::pushActivity(new ChildActivity(a, b, [this] { rebuild(); }), brls::TransitionAnimation::NONE);
            } else if (pick == Pick::QR) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new QrShowActivity(mii), brls::TransitionAnimation::NONE);
            } else {
                openEditor(miis[index]);
            }
            return true;
        });
        card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
        card->getFocusEvent()->subscribe([this, index](brls::View*) {
            thumbPriorities(index);
            if (held >= 0)
                showHeldAt(index);
        });
        row->addView(card);
    }
    // up / down go to the card straight above or below (borealis would go
    // back to the last card focused in that row); a short last row takes
    // its last card
    const int n = (int)cards.size();
    for (int i = 0; i < n; i++) {
        if (i >= kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::UP, cards[i - kColumns]);
        if (i / kColumns < (n - 1) / kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, cards[std::min(i + kColumns, n - 1)]);
    }
    // up from the first row: the Add Mii button; down from the buttons: the
    // first Mii
    for (int i = 0; i < std::min(n, kColumns); i++)
        cards[i]->setCustomNavigationRoute(brls::FocusDirection::UP, createButton);
    for (brls::View* b : topButtons) {
        if (n > 0)
            b->setCustomNavigationRoute(brls::FocusDirection::DOWN, cards[0]);
        else
            b->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
    }
    allCardActions();
    thumbPriorities(0);
}

// Every card's buttons, then one hints update (one per card made picking a
// Mii up and putting it down stutter).
void LibraryActivity::allCardActions()
{
    batchingHints = true;
    for (size_t i = 0; i < cards.size(); i++)
        cardActions((int)i);
    batchingHints = false;
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

// START (settings) is there only in the plain list: not while a Mii is held
// or one is being chosen. Changed after this input is handled (the frame may
// be going through its actions right now).
void LibraryActivity::updateSettingsAction()
{
    brls::sync([this] {
        const bool want = held < 0 && pick == Pick::NONE;
        if (want != settingsShown)
            settingsAction(want);
    });
}

// Thumbnails nearest the cursor are drawn first.
void LibraryActivity::thumbPriorities(int focus)
{
    const int fr = focus / kColumns, fc = focus % kColumns;
    for (size_t i = 0; i < thumbs.size(); i++) {
        const int r = (int)i / kColumns, c = (int)i % kColumns;
        HeadRenderer::instance().setPriority(thumbs[i], 2 * std::abs(r - fr) + std::abs(c - fc));
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
    if (held >= 0) {
        card->updateActionHint(brls::BUTTON_A, "library/place"_i18n);
        ids.push_back(card->registerAction("library/swap"_i18n, brls::BUTTON_Y, [this, index](brls::View*) {
            swapHeld(index);
            return true;
        }));
        ids.push_back(card->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](brls::View*) {
            endHold(false);
            return true;
        }));
    } else {
        card->updateActionHint(brls::BUTTON_A, pick == Pick::EXPORT ? "library/export"_i18n
                : pick == Pick::QR                                      ? "library/qr"_i18n
                : pick == Pick::SEND                                    ? "library/send_this"_i18n
                : pick == Pick::PARENT1 || pick == Pick::PARENT2 || pick == Pick::KID ? "library/child_choose"_i18n
                                                                        : "library/edit"_i18n);
        if (pick != Pick::NONE) {
            // choosing a Mii: Circle ends it (Done after exporting several);
            // no deleting or picking up meanwhile
            ids.push_back(card->registerAction(pick == Pick::EXPORT ? "library/pick_done"_i18n : "hints/cancel"_i18n,
                brls::BUTTON_B, [this](brls::View*) {
                    setPick(Pick::NONE);
                    return true;
                }));
            if (!batchingHints)
                brls::Application::getGlobalHintsUpdateEvent()->fire();
            return;
        }
        ids.push_back(card->registerAction("library/delete"_i18n, brls::BUTTON_X, [this, index](brls::View*) {
            auto* dialog = new brls::Dialog("library/delete_confirm"_i18n);
            dialog->addButton("hints/cancel"_i18n, [] {});
            dialog->addButton("library/delete"_i18n, [this, index] {
                if (!MiiLibrary::instance().remove(index))
                    brls::Application::notify("library/delete_failed"_i18n);
                // Rebuild after the dialog has closed and released focus.
                brls::sync([this] { rebuild(); });
            });
            dialog->open();
            return true;
        }));
        if (pick == Pick::NONE)
            ids.push_back(card->registerAction("library/pick_up"_i18n, brls::BUTTON_Y, [this, index](brls::View*) {
                startHold(index);
                return true;
            }));
    }
    if (!batchingHints)
        brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void LibraryActivity::startHold(int index)
{
    held = index;
    heldMii = miis[index];
    if (!floating) {
        floating.reset(new MiiPreview(&heldMii, 160));
        floating->setDimensions(200, 250);
    }
    thumbs[index]->setVisibility(brls::Visibility::INVISIBLE); // its place is empty while held
    // the lifted card has its own outline; the one under it is covered
    for (brls::View* c : cards)
        c->setHideHighlight(true);
    static_cast<MiiCard*>(cards[index])->empty = true;
    showHeldAt(index);
    // no settings while holding a Mii (nor their hint), and the cursor stays
    // in the list
    updateSettingsAction();
    for (brls::View* b : topButtons)
        b->setFocusable(false);
    allCardActions();
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

// Rereads the order from the library into the cards (their previews point
// into `miis`), without rebuilding the screen.
void LibraryActivity::reloadOrder()
{
    MiiLibrary& lib = MiiLibrary::instance();
    for (size_t i = 0; i < miis.size(); i++)
        lib.get((int)i, &miis[i]);
}

void LibraryActivity::endHold(bool place)
{
    const int from = held, to = heldAt;
    held = heldAt = -1;
    updateSettingsAction();
    for (brls::View* b : topButtons)
        b->setFocusable(true);
    for (brls::View* c : cards) {
        static_cast<MiiCard*>(c)->overlay = nullptr;
        c->setHideHighlight(false);
    }
    if (from >= 0 && from < (int)thumbs.size()) {
        thumbs[from]->setVisibility(brls::Visibility::VISIBLE);
        static_cast<MiiCard*>(cards[from])->empty = false;
    }
    // Put down on another card: it moves there and the Mii between close
    // up, taking the place it left.
    if (place && from != to && to >= 0) {
        if (!MiiLibrary::instance().move(from, to))
            brls::Application::notify("library/move_failed"_i18n);
        reloadOrder();
    }
    allCardActions();
}

// Puts the held Mii where the hovered one is and picks that one up (it
// now belongs in the empty place the held one left).
void LibraryActivity::swapHeld(int index)
{
    if (index == held)
        return;
    if (!MiiLibrary::instance().swap(held, index)) {
        brls::Application::notify("library/move_failed"_i18n);
        return;
    }
    reloadOrder();
    heldMii = miis[held];
    showHeldAt(index);
}

void LibraryActivity::importMiis()
{
    MiiLibrary& lib = MiiLibrary::instance();
    const MiiLibrary::ImportResult r = lib.importFolder();
    std::string msg;
    if (r.imported == 0 && r.failed == 0 && !r.full)
        msg = "library/import_none"_i18n + " " + lib.importDir() + "/";
    else {
        msg = std::to_string(r.imported) + " " + "library/import_done"_i18n;
        if (r.failed)
            msg += ", " + std::to_string(r.failed) + " " + "library/import_failed"_i18n;
        if (r.full)
            msg += ". " + "library/import_full"_i18n;
    }
    brls::Application::notify(msg);
    if (r.imported)
        brls::sync([this] { rebuild(); });
}

void LibraryActivity::exportMii(int index)
{
    std::string path;
    if (MiiLibrary::instance().exportMii(index, &path))
        brls::Application::notify("library/exported"_i18n + " " + path);
    else
        brls::Application::notify("library/export_failed"_i18n);
}

// A Mii read from a QR code joins the library (QR codes carry no create id,
// so it gets a new one).
void LibraryActivity::addScanned(const mii_charinfo_t& scanned)
{
    mii_charinfo_t mii = scanned;
    MiiLibrary& lib = MiiLibrary::instance();
    if (!mii_create_id_is_valid(&mii.create_id))
        lib.newCreateId(&mii.create_id);
    char name[64];
    mii_name_get_utf8(&mii, name, sizeof(name));
    if (lib.put(&mii) < 0) {
        brls::Application::notify("library/import_full"_i18n);
        return;
    }
    brls::Application::notify("qr/added"_i18n + " " + name);
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
    pick = mode;
    pickLabel->setText(mode == Pick::EXPORT ? "library/pick_export"_i18n
            : mode == Pick::QR              ? "library/pick_qr"_i18n
            : mode == Pick::SEND            ? "library/pick_send"_i18n
            : mode == Pick::KID             ? "library/pick_kid"_i18n
            : mode == Pick::PARENT1         ? "library/pick_parent1"_i18n
            : mode == Pick::PARENT2         ? parent2Prompt()
                                            : "");
    pickLabel->setVisibility(mode == Pick::NONE ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    rebuild();
    // choosing a Mii starts on the first one
    if (mode != Pick::NONE && !cards.empty())
        brls::Application::giveFocus(cards[0]);
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
            brls::Application::pushActivity(new StartActivity([this](const mii_charinfo_t& ci) { openEditor(ci); }, false),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_lookalike"_i18n, [this] {
            brls::Application::pushActivity(new StartActivity([this](const mii_charinfo_t& ci) { openEditor(ci); }, true),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_kid"_i18n, [this] {
            if (miis.empty()) {
                brls::Application::notify("library/empty"_i18n);
                return;
            }
            setPick(Pick::KID);
        }),
        PopupMenu::item("library/add_child"_i18n, [this] {
            if (miis.size() < 2) {
                brls::Application::notify("library/child_need_two"_i18n);
                return;
            }
            parentAIndex = -1;
            setPick(Pick::PARENT1);
        }),
    };
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
            brls::Application::pushActivity(new QrScanActivity([this](const mii_charinfo_t& mii) { addScanned(mii); }),
                brls::TransitionAnimation::NONE);
        }),
        PopupMenu::item("library/add_receive"_i18n, [this] {
            brls::Application::pushActivity(new TradeActivity([this] { stale = true; }),
                brls::TransitionAnimation::NONE);
        }),
    };
    brls::Application::pushActivity(new PopupMenu("library/receive"_i18n, items), brls::TransitionAnimation::NONE);
}

void LibraryActivity::showSendMenu()
{
    if (miis.empty()) {
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
                    rebuild();
                    brls::Application::notify("settings/reset_cache_done"_i18n);
                });
            });
            dialog->open();
        });
    });
    brls::Application::pushActivity(
        new PopupMenu("settings/title"_i18n, { skin, theme, musicItem, qrFormat, curated, resetCache }, focus),
        brls::TransitionAnimation::NONE);
}
