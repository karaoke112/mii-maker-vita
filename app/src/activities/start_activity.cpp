#include "activities/start_activity.hpp"

#include "ui_theme.hpp"
#include "views/controls.hpp"
#include "views/mii_preview.hpp"

using namespace brls::literals;

StartActivity::StartActivity(OnPicked onPicked)
    : onPicked(std::move(onPicked))
{
    for (int g = 0; g < 2; g++)
        mii_charinfo_init_default(&bases[g], g);
}

brls::View* StartActivity::createContentView()
{
    auto* page = new brls::Box(brls::Axis::COLUMN);
    page->setAlignItems(brls::AlignItems::CENTER);
    page->setJustifyContent(brls::JustifyContent::CENTER);
    page->setGrow(1);

    page->addView(makePrompt("start/prompt"_i18n));

    auto* row = new brls::Box(brls::Axis::ROW);
    for (int g = 0; g < 2; g++) {
        auto* tile = new brls::Box(brls::Axis::COLUMN);
        tile->setDimensions(280, 340);
        if (g)
            tile->setMarginLeft(16);
        tile->setBackgroundColor(ui::kTile);
        tile->setCornerRadius(4);
        tile->setHighlightCornerRadius(6);
        tile->setFocusable(true);
        auto* preview = new MiiPreview(&bases[g], 256);
        preview->setStage(true);
        preview->setInteractive(false);
        preview->setFraming(MiiPreview::Framing::FULL_BODY);
        preview->setDimensions(280, 340);
        preview->setZoom(1.35f);
        tile->addView(preview);
        tile->registerClickAction([this, g](brls::View*) {
            OnPicked cb = onPicked;
            // leave this screen first, then open the editor
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb, g] {
                if (cb)
                    cb(g);
            });
            return true;
        });
        tile->addGestureRecognizer(new brls::TapGestureRecognizer(tile));
        row->addView(tile);
    }
    page->addView(row);

    auto* note = new brls::Label();
    note->setText("editor/prompt/style_note"_i18n);
    note->setFontSize(20);
    note->setMarginTop(28);
    page->addView(note);

    auto* frame = new brls::AppletFrame(page);
    frame->setTitle("start/title"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    return frame;
}
