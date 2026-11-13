#include "activities/mii_view_activity.hpp"

#include "activities/child_activity.hpp"
#include "activities/editor_activity.hpp"
#include "activities/photo_activity.hpp"
#include "activities/qr_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "mii_qr.hpp"
#include "ui_theme.hpp"
#include "views/mii_preview.hpp"
#include "views/popup_menu.hpp"

using namespace brls::literals;

MiiViewActivity::MiiViewActivity(std::vector<mii_charinfo_t> miis, int index, std::function<void(int, bool)> onClosed)
    : miis(std::move(miis))
    , index(index)
    , onClosed(std::move(onClosed))
{
    current = this->miis[index];
}

brls::View* MiiViewActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);

    // the whole Mii, as the editor's stage draws it (with its shadow)
    auto* preview = new MiiPreview(&current, 384);
    preview->setStage(true);
    preview->setFraming(MiiPreview::Framing::FULL_BODY);
    preview->setYawSource(&yaw);
    preview->setPitchSource(&pitch);
    preview->setFrontSource(&facingFront);
    preview->setExpressionSource(&expression);
    preview->setPoseSource(&pose);
    preview->setFavoriteSource(&favorite);
    preview->setWidth(560);
    preview->setGrow(0);
    root->addView(preview);

    auto* right = new brls::Box(brls::Axis::COLUMN);
    right->setGrow(1);
    right->setJustifyContent(brls::JustifyContent::CENTER);
    right->setPadding(0, 50, 0, 20);
    name = new brls::Label();
    name->setFontSize(40);
    right->addView(name);
    position = new brls::Label();
    position->setFontSize(20);
    position->setTextColor(ui::textDim());
    position->setMarginBottom(24);
    right->addView(position);

    std::vector<PopupMenu::Item> items;
    items.push_back(PopupMenu::item("view/edit"_i18n, [this] { edit(); }));
    PopupMenu::Item expr = PopupMenu::item("view/expression"_i18n, [this] { showExpressionMenu(&expression); });
    expr.value = [this] { return brls::getStr("expressions/names/" + std::to_string(expression)); };
    items.push_back(expr);
    PopupMenu::Item poseItem = PopupMenu::item("view/pose"_i18n, [this] { showPoseMenu(&pose); });
    poseItem.value = [this] { return poseDisplayName(pose); };
    items.push_back(poseItem);
    items.push_back(PopupMenu::item("view/photo"_i18n, [this] {
        brls::Application::pushActivity(new PhotoActivity(current, favorite, &expression, &pose),
            brls::TransitionAnimation::NONE);
    }));
    items.push_back(PopupMenu::item("view/qr"_i18n, [this] {
        if (!qr::available()) {
            auto* dialog = new brls::Dialog("qr/need_key"_i18n + "\n\n" + qr::keyPath());
            dialog->addButton("hints/ok"_i18n, [] {});
            dialog->open();
            return;
        }
        brls::Application::pushActivity(new QrShowActivity(current), brls::TransitionAnimation::NONE);
    }));
    items.push_back(PopupMenu::item("view/kid"_i18n, [this] {
        brls::Application::pushActivity(new ChildActivity(current, [this] { changed = true; }),
            brls::TransitionAnimation::NONE);
    }));
    items.push_back(PopupMenu::item("view/delete"_i18n, [this] { remove(); }));
    for (size_t i = 0; i < items.size(); i++)
        right->addView(PopupMenu::row(items[i], i == 0));
    right->setDefaultFocusedIndex(2); // Edit (after the name and position)
    root->addView(right);

    auto* frame = new brls::AppletFrame(root);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->registerAction("editor/rotate"_i18n, brls::BUTTON_RSB, [](brls::View*) { return true; });
    frame->registerAction("editor/reset_position"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        facingFront = true;
        return true;
    });
    frame->registerAction("view/previous"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        step(-1);
        return true;
    });
    frame->registerAction("view/next"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        step(1);
        return true;
    });
    // desktop builds: Q / E
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Q), [this](brls::View*) {
        step(-1);
        return true;
    });
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_E), [this](brls::View*) {
        step(1);
        return true;
    });
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
        close();
        return true;
    });
    show(index);
    return frame;
}

void MiiViewActivity::show(int i)
{
    index = i;
    current = miis[i]; // the preview notices and redraws
    favorite = MiiLibrary::instance().details(current.create_id).favorite;
    char text[64];
    mii_name_get_utf8(&current, text, sizeof(text));
    name->setText(text);
    position->setText(std::to_string(i + 1) + " / " + std::to_string(miis.size()));
}

// L / R: the Mii before or after this one in the list (round at the ends)
void MiiViewActivity::step(int delta)
{
    const int n = (int)miis.size();
    if (n > 1)
        show(((index + delta) % n + n) % n);
}

void MiiViewActivity::edit()
{
    brls::Application::pushActivity(new EditorActivity(current, [this](const mii_charinfo_t& saved) {
        miis[index] = saved;
        changed = true;
        show(index);
    }),
        brls::TransitionAnimation::NONE);
}

void MiiViewActivity::remove()
{
    auto* dialog = new brls::Dialog("library/delete_confirm"_i18n);
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("library/delete"_i18n, [this] {
        if (!MiiLibrary::instance().remove(index)) {
            brls::Application::notify("library/delete_failed"_i18n);
            return;
        }
        changed = true;
        miis.erase(miis.begin() + index);
        // after the dialog has closed: the next Mii, or back to the list
        brls::sync([this] {
            if (miis.empty())
                close();
            else
                show(std::min(index, (int)miis.size() - 1));
        });
    });
    dialog->open();
}

void MiiViewActivity::close()
{
    auto cb = onClosed;
    const int i = index;
    const bool c = changed;
    brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb, i, c] {
        if (cb)
            cb(i, c);
    });
}
