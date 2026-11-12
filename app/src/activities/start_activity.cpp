#include "activities/start_activity.hpp"

#include "settings.hpp"

#include "ui_theme.hpp"
#include "views/controls.hpp"
#include "views/mii_preview.hpp"

#include <mii/mii_editor.h>

using namespace brls::literals;

StartActivity::StartActivity(OnPicked onPicked, bool lookalike)
    : onPicked(std::move(onPicked))
    , lookalike(lookalike)
    , choices(lookalike ? 10 : 2)
    , seed((uint32_t)brls::getCPUTimeUsec() | 1u)
{
    if (lookalike) {
        shuffle();
    } else {
        for (int g = 0; g < 2; g++) {
            mii_charinfo_init_default(&choices[g], g);
            choices[g].faceline_color = Settings::get().skinTone; // Settings: default skin tone
        }
    }
}

void StartActivity::shuffle()
{
    // five of each style
    for (size_t i = 0; i < choices.size(); i++)
        mii_charinfo_init_random(&choices[i], (int)(i % 2), &seed);
}

brls::View* StartActivity::createContentView()
{
    auto* page = new brls::Box(brls::Axis::COLUMN);
    page->setAlignItems(brls::AlignItems::CENTER);
    page->setJustifyContent(brls::JustifyContent::CENTER);
    page->setGrow(1);

    page->addView(makePrompt(lookalike ? "start/prompt_lookalike"_i18n : "start/prompt"_i18n));

    const int columns = lookalike ? 5 : 2;
    const float w = lookalike ? 170 : 280, h = lookalike ? 200 : 340;
    brls::Box* row = nullptr;
    std::vector<brls::Box*> tiles;
    for (size_t i = 0; i < choices.size(); i++) {
        if (i % columns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            if (i)
                row->setMarginTop(14);
            page->addView(row);
        }
        auto* tile = new brls::Box(brls::Axis::COLUMN);
        tile->setDimensions(w, h);
        if (i % columns)
            tile->setMarginLeft(14);
        tile->setBackgroundColor(ui::tile());
        tile->setCornerRadius(4);
        tile->setHighlightCornerRadius(6);
        tile->setFocusable(true);
        auto* preview = new MiiPreview(&choices[i], lookalike ? 192 : 256);
        preview->setStage(true);
        preview->setInteractive(false);
        preview->setBackdrop(ui::tile());
        preview->setFraming(lookalike ? MiiPreview::Framing::BUST : MiiPreview::Framing::FULL_BODY);
        preview->setDimensions(w, h);
        if (!lookalike)
            preview->setZoom(1.35f);
        tile->addView(preview);
        tile->registerClickAction([this, i](brls::View*) {
            OnPicked cb = onPicked;
            const mii_charinfo_t picked = choices[i];
            // leave this screen first, then open the editor
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb, picked] {
                if (cb)
                    cb(picked);
            });
            return true;
        });
        tile->addGestureRecognizer(new brls::TapGestureRecognizer(tile));
        row->addView(tile);
        tiles.push_back(tile);
    }
    // up / down keep the column
    for (size_t i = 0; i < tiles.size(); i++) {
        if (i >= (size_t)columns)
            tiles[i]->setCustomNavigationRoute(brls::FocusDirection::UP, tiles[i - columns]);
        if (i + columns < tiles.size())
            tiles[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, tiles[i + columns]);
    }

    if (!lookalike) {
        auto* note = new brls::Label();
        note->setText("editor/prompt/style_note"_i18n);
        note->setFontSize(20);
        note->setMarginTop(28);
        page->addView(note);
    }

    auto* frame = new brls::AppletFrame(page);
    frame->setTitle(lookalike ? "start/title_lookalike"_i18n : "start/title"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    if (lookalike)
        frame->registerAction("start/shuffle"_i18n, brls::BUTTON_Y, [this](brls::View*) {
            shuffle(); // the previews notice the change and redraw
            return true;
        });
    return frame;
}
