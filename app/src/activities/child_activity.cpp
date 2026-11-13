#include "activities/child_activity.hpp"

#include "activities/editor_activity.hpp"
#include "mii_library.hpp"
#include "ui_theme.hpp"
#include "views/controls.hpp"
#include "views/mii_preview.hpp"

using namespace brls::literals;

static std::string miiName(const mii_charinfo_t& m)
{
    char name[64];
    mii_name_get_utf8(&m, name, sizeof(name));
    return name;
}

ChildActivity::ChildActivity(const mii_charinfo_t& a, const mii_charinfo_t& b, std::function<void()> onClosed)
    : parentA(a)
    , parentB(b)
    , onClosed(std::move(onClosed))
{
    generate();
}

void ChildActivity::generate()
{
    std::array<mii_create_id_t, child::kStages> ids;
    for (mii_create_id_t& id : ids)
        MiiLibrary::instance().newCreateId(&id);
    stages = child::generate(parentA, parentB, ids);
    saved.fill(false);
}

void ChildActivity::refreshLabels()
{
    for (int i = 0; i < child::kStages; i++) {
        if (names[i])
            names[i]->setText(miiName(stages[i]));
        if (marks[i])
            marks[i]->setVisibility(saved[i] ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
    }
}

brls::View* ChildActivity::createContentView()
{
    auto* page = new brls::Box(brls::Axis::COLUMN);
    page->setAlignItems(brls::AlignItems::CENTER);
    page->setJustifyContent(brls::JustifyContent::CENTER);
    page->setGrow(1);

    page->addView(makePrompt(brls::getStr("child/prompt", miiName(parentA), miiName(parentB))));

    static const char* const kStageNames[child::kStages] = { "child/stage0", "child/stage1", "child/stage2",
                                                             "child/stage3", "child/stage4", "child/stage5" };
    const float w = 142, h = 270;
    auto* row = new brls::Box(brls::Axis::ROW);
    for (int i = 0; i < child::kStages; i++) {
        auto* column = new brls::Box(brls::Axis::COLUMN);
        column->setAlignItems(brls::AlignItems::CENTER);
        if (i)
            column->setMarginLeft(12);

        auto* stage = new brls::Label();
        stage->setText(brls::getStr(kStageNames[i]));
        stage->setFontSize(18);
        stage->setTextColor(ui::textDim());
        stage->setMarginBottom(6);
        column->addView(stage);

        auto* tile = new brls::Box(brls::Axis::COLUMN);
        tile->setDimensions(w, h);
        tile->setBackgroundColor(ui::tile());
        tile->setCornerRadius(4);
        tile->setHighlightCornerRadius(6);
        tile->setFocusable(true);
        auto* preview = new MiiPreview(&stages[i], 256);
        preview->setStage(true);
        preview->setCompact(true);
        preview->setInteractive(false);
        preview->setBackdrop(ui::tile());
        // the whole Mii, all six in one framing: the child grows
        preview->setFraming(MiiPreview::Framing::FULL_BODY);
        preview->setZoom(1.35f);
        preview->setDimensions(w, h);
        tile->addView(preview);
        tile->registerClickAction([this, i](brls::View*) {
            // edit (and save) this stage; Done comes back here with it
            brls::Application::pushActivity(new EditorActivity(stages[i],
                                                [this, i](const mii_charinfo_t& m) {
                                                    stages[i] = m;
                                                    saved[i] = true;
                                                    refreshLabels();
                                                }),
                brls::TransitionAnimation::NONE);
            return true;
        });
        tile->addGestureRecognizer(new brls::TapGestureRecognizer(tile));
        tile->updateActionHint(brls::BUTTON_A, "library/edit"_i18n);
        column->addView(tile);

        names[i] = new brls::Label();
        names[i]->setFontSize(20);
        names[i]->setMarginTop(8);
        names[i]->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        column->addView(names[i]);

        marks[i] = new brls::Label();
        marks[i]->setText("child/saved"_i18n);
        marks[i]->setFontSize(16);
        marks[i]->setTextColor(ui::accent());
        column->addView(marks[i]);

        row->addView(column);
    }
    page->addView(row);
    refreshLabels();

    auto* frame = new brls::AppletFrame(page);
    frame->setTitle("child/title"_i18n);
    frame->registerAction("child/again"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        generate(); // the previews notice the change and redraw
        refreshLabels();
        return true;
    });
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
        auto* dialog = new brls::Dialog("child/leave"_i18n);
        dialog->addButton("hints/cancel"_i18n, [] {});
        dialog->addButton("child/leave_yes"_i18n, [this] {
            auto cb = onClosed;
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb] {
                if (cb)
                    cb(); // the Mii list shows the stages saved
            });
        });
        dialog->open();
        return true;
    });
    return frame;
}
