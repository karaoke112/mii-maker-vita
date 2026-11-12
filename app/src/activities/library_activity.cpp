#include "activities/library_activity.hpp"

#include "activities/editor_activity.hpp"
#include "activities/start_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
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
    pickLabel->setTextColor(nvgRGB(10, 100, 220));
    pickLabel->setMarginLeft(30);
    pickLabel->setVisibility(brls::Visibility::GONE);
    actions->addView(pickLabel);
    content->addView(actions);

    // Tell the user where the 3D face resource goes (or that it's in use).
    auto* status = new brls::Label();
    status->setText(HeadRenderer::instance().status());
    status->setFontSize(16);
    status->setTextColor(nvgRGB(110, 110, 125));
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
            } else {
                openEditor(miis[index]);
            }
            return true;
        });
        card->updateActionHint(brls::BUTTON_A, pick == Pick::EXPORT ? "library/export"_i18n : "library/edit"_i18n);
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

// "Choose a Mii" mode: the next card pressed is exported instead of edited.
void LibraryActivity::setPick(Pick mode)
{
    pick = mode;
    pickLabel->setText(mode == Pick::EXPORT ? "library/pick_export"_i18n : "");
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

static void comingSoon()
{
    brls::Application::notify("library/coming_soon"_i18n);
}

void LibraryActivity::showAddMenu()
{
    std::vector<std::string> items = { "library/add_scratch"_i18n, "library/add_lookalike"_i18n,
        "library/add_import"_i18n, "library/add_qr"_i18n, "library/add_receive"_i18n };
    auto* menu = new brls::Dropdown("library/add"_i18n, items, [this](int selected) {
        // act once the menu has closed
        brls::sync([this, selected] {
            switch (selected) {
            case 0:
                brls::Application::pushActivity(new StartActivity([this](int gender) {
                    mii_charinfo_t ci;
                    mii_charinfo_init_default(&ci, gender);
                    openEditor(ci);
                }), brls::TransitionAnimation::NONE);
                break;
            case 2:
                importMiis();
                break;
            default:
                comingSoon();
                break;
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(menu));
}

void LibraryActivity::showSendMenu()
{
    if (miis.empty()) {
        brls::Application::notify("library/empty"_i18n);
        return;
    }
    std::vector<std::string> items = { "library/send_export"_i18n, "library/send_qr"_i18n, "library/send_vita"_i18n };
    auto* menu = new brls::Dropdown("library/send"_i18n, items, [this](int selected) {
        brls::sync([this, selected] {
            if (selected == 0)
                setPick(Pick::EXPORT);
            else
                comingSoon();
        });
    });
    brls::Application::pushActivity(new brls::Activity(menu));
}
