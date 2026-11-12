#include "activities/library_activity.hpp"

#include "activities/editor_activity.hpp"
#include "activities/qr_activity.hpp"
#include "activities/trade_activity.hpp"
#include "activities/start_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "mii_qr.hpp"
#include "settings.hpp"
#include "ui_theme.hpp"
#include "views/popup_menu.hpp"
#include "views/mii_preview.hpp"

using namespace brls::literals;

static constexpr int kColumns = 5;

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
    add->setWidth(260);
    add->setMarginRight(20);
    add->registerClickAction([this](brls::View*) {
        showAddMenu();
        return true;
    });
    actions->addView(add);
    createButton = add;
    auto* send = new brls::Button();
    send->setText("library/send"_i18n);
    send->setWidth(260);
    send->registerClickAction([this](brls::View*) {
        showSendMenu();
        return true;
    });
    actions->addView(send);
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
    // This is the root screen: "back" only leaves "choose a Mii" mode (popping
    // the last activity would leave borealis with input blocked). The app is
    // closed from the system (PS button), like other Vita apps.
    frame->registerAction("settings/title"_i18n, brls::BUTTON_START, [this](brls::View*) {
        showSettings();
        return true;
    });
    // desktop builds: P (keyboards have no START)
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_P), [this](brls::View*) {
        showSettings();
        return true;
    });
    frame->registerAction("", brls::BUTTON_B, [this](brls::View*) {
        if (pick != Pick::NONE)
            setPick(Pick::NONE);
        return true;
    }, true);
    return frame;
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
    brls::Application::pushActivity(new EditorActivity(start, [this] { rebuild(); }), brls::TransitionAnimation::NONE);
}

void LibraryActivity::rebuild()
{
    MiiLibrary& lib = MiiLibrary::instance();

    // The cards are about to be freed; never leave focus on one of them.
    brls::Application::giveFocus(createButton);
    grid->clearViews();
    cards.clear();
    miis.clear();
    miis.reserve(MII_NFDB_MAX_MIIS); // previews keep pointers into this vector
    for (int i = 0; i < lib.count(); i++) {
        mii_charinfo_t ci;
        if (lib.get(i, &ci))
            miis.push_back(ci);
    }
    empty->setVisibility(miis.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    brls::Box* row = nullptr;
    for (size_t i = 0; i < miis.size(); i++) {
        if (i % kColumns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(20);
            grid->addView(row);
        }
        auto* card = new brls::Box(brls::Axis::COLUMN);
        cards.push_back(card);
        card->setDimensions(200, 250);
        card->setMarginRight(20);
        card->setFocusable(true);
        card->setCornerRadius(18);
        card->setHighlightCornerRadius(20);
        auto* preview = new MiiPreview(&miis[i], 160);
        preview->setDimensions(200, 250);
        card->addView(preview);

        const int index = (int)i;
        card->registerClickAction([this, index](brls::View*) {
            if (pick == Pick::EXPORT) {
                exportMii(index);
                setPick(Pick::NONE);
            } else if (pick == Pick::SEND) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new TradeActivity(mii), brls::TransitionAnimation::NONE);
            } else if (pick == Pick::QR) {
                const mii_charinfo_t mii = miis[index];
                setPick(Pick::NONE);
                brls::Application::pushActivity(new QrShowActivity(mii), brls::TransitionAnimation::NONE);
            } else {
                openEditor(miis[index]);
            }
            return true;
        });
        card->updateActionHint(brls::BUTTON_A, pick == Pick::EXPORT ? "library/export"_i18n
                : pick == Pick::QR                                      ? "library/qr"_i18n
                : pick == Pick::SEND                                    ? "library/send_this"_i18n
                                                                        : "library/edit"_i18n);
        card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
        card->registerAction("library/delete"_i18n, brls::BUTTON_X, [this, index](brls::View*) {
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
        });
        row->addView(card);
    }
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

// "Choose a Mii" mode: the next card pressed is exported instead of edited.
void LibraryActivity::setPick(Pick mode)
{
    pick = mode;
    pickLabel->setText(mode == Pick::EXPORT ? "library/pick_export"_i18n
            : mode == Pick::QR              ? "library/pick_qr"_i18n
            : mode == Pick::SEND            ? "library/pick_send"_i18n
                                            : "");
    pickLabel->setVisibility(mode == Pick::NONE ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    brls::View* focus = brls::Application::getCurrentFocus();
    int focusIndex = -1;
    for (size_t i = 0; i < cards.size(); i++)
        if (cards[i] == focus)
            focusIndex = (int)i;
    rebuild();
    if (mode != Pick::NONE && !cards.empty())
        brls::Application::giveFocus(cards[focusIndex >= 0 ? focusIndex : 0]);
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
    brls::Application::pushActivity(new PopupMenu("library/add"_i18n, items), brls::TransitionAnimation::NONE);
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

void LibraryActivity::showSettings()
{
    PopupMenu::Item theme;
    theme.text = [] {
        return "settings/theme"_i18n + ": " +
            (Settings::get().theme == ui::Theme::DARK ? "settings/dark"_i18n : "settings/light"_i18n);
    };
    // Picking it closes the menu; the screen is then rebuilt in the new
    // theme and the menu opens again, so everything switches at once.
    theme.action = [this] {
        Settings& s = Settings::get();
        s.theme = s.theme == ui::Theme::DARK ? ui::Theme::LIGHT : ui::Theme::DARK;
        s.save();
        ui::applyTheme(s.theme);
        ui::applyVariant();
        rebuildForTheme();
        showSettings();
    };
    PopupMenu::Item renderer;
    renderer.text = [] {
        return "settings/renderer"_i18n + ": " + (Settings::get().gpu ? "settings/gpu"_i18n : "settings/software"_i18n);
    };
    renderer.action = [] {
        Settings& s = Settings::get();
        s.gpu = !s.gpu;
        s.save();
        brls::Application::notify("settings/restart"_i18n);
    };
    renderer.keepOpen = true;
    brls::Application::pushActivity(new PopupMenu("settings/title"_i18n, { theme, renderer }), brls::TransitionAnimation::NONE);
}
