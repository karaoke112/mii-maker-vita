#include "activities/library_activity.hpp"

#include "activities/editor_activity.hpp"
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
    for (int gender = 0; gender < 2; gender++) {
        auto* button = new brls::Button();
        button->setText(gender ? "library/new_female"_i18n : "library/new_male"_i18n);
        button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        button->setWidth(300);
        button->setMarginRight(20);
        button->registerClickAction([this, gender](brls::View*) {
            mii_charinfo_t ci;
            mii_charinfo_init_default(&ci, gender);
            openEditor(ci);
            return true;
        });
        actions->addView(button);
        if (!createButton)
            createButton = button;
    }
    content->addView(actions);

    empty = new brls::Label();
    empty->setText("library/empty"_i18n);
    empty->setFontSize(22);
    content->addView(empty);

    grid = new brls::Box(brls::Axis::COLUMN);
    content->addView(grid);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("library/title"_i18n);
    frame->registerAction("hints/exit"_i18n, brls::BUTTON_START, [](brls::View*) {
        brls::Application::quit();
        return true;
    });
    // This is the root screen: "back" has nowhere to go, and popping the last
    // activity leaves borealis with input blocked.
    frame->registerAction("", brls::BUTTON_B, [](brls::View*) { return true; }, true);
    return frame;
}

void LibraryActivity::onContentAvailable()
{
    rebuild();
}

void LibraryActivity::openEditor(const mii_charinfo_t& start)
{
    brls::Application::pushActivity(new EditorActivity(start, [this] { rebuild(); }));
}

void LibraryActivity::rebuild()
{
    MiiLibrary& lib = MiiLibrary::instance();

    // The cards are about to be freed; never leave focus on one of them.
    brls::Application::giveFocus(createButton);
    grid->clearViews();
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
        card->setDimensions(200, 250);
        card->setMarginRight(20);
        card->setFocusable(true);
        card->setCornerRadius(18);
        card->setHighlightCornerRadius(20);
        auto* preview = new MiiPreview(&miis[i]);
        preview->setDimensions(200, 250);
        card->addView(preview);

        const int index = (int)i;
        card->registerClickAction([this, index](brls::View*) {
            openEditor(miis[index]);
            return true;
        });
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
        card->registerAction("library/export"_i18n, brls::BUTTON_Y, [index](brls::View*) {
            std::string path;
            if (MiiLibrary::instance().exportMii(index, &path))
                brls::Application::notify("library/exported"_i18n + " " + path);
            else
                brls::Application::notify("library/export_failed"_i18n);
            return true;
        });
        row->addView(card);
    }
}
