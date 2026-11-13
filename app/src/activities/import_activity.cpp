#include "activities/import_activity.hpp"

#include "ui_theme.hpp"
#include "views/mii_preview.hpp"

using namespace brls::literals;

namespace {

constexpr int kColumns = 5;

// A found Mii's card: dimmed and marked when left out or to be deleted.
class ImportCard : public brls::Box
{
  public:
    ImportCard()
        : brls::Box(brls::Axis::COLUMN)
    {
    }
    int state = 0; // ImportActivity::State

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        if (state == 0)
            return;
        // a veil over the Mii, and what happens to its file
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 18);
        nvgFillColor(vg, state == 2 ? nvgRGBA(200, 40, 40, 110) : nvgRGBA(128, 128, 128, 140));
        nvgFill(vg);
        const std::string label = state == 2 ? "import/will_delete"_i18n : "import/left_out"_i18n;
        nvgFontSize(vg, 24);
        nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        const float bw = nvgTextBounds(vg, 0, 0, label.c_str(), nullptr, nullptr) + 28;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + (width - bw) / 2, y + height / 2 - 20, bw, 40, 20);
        nvgFillColor(vg, state == 2 ? nvgRGB(200, 40, 40) : nvgRGB(90, 90, 90));
        nvgFill(vg);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgText(vg, x + width / 2, y + height / 2, label.c_str(), nullptr);
    }
};

} // namespace

ImportActivity::ImportActivity(std::vector<MiiLibrary::Pending> pending, int unreadable, std::function<void(int)> onDone)
    : pending(std::move(pending))
    , states(this->pending.size(), IMPORT)
    , unreadable(unreadable)
    , onDone(std::move(onDone))
{
}

brls::View* ImportActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(20, 40, 20, 40);
    summary = new brls::Label();
    summary->setFontSize(22);
    summary->setTextColor(ui::textDim());
    summary->setMarginBottom(20);
    content->addView(summary);

    brls::Box* row = nullptr;
    for (size_t i = 0; i < pending.size(); i++) {
        if (i % kColumns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(20);
            content->addView(row);
        }
        auto* card = new ImportCard();
        card->setDimensions(200, 250);
        card->setMarginRight(20);
        card->setFocusable(true);
        card->setCornerRadius(18);
        card->setHighlightCornerRadius(20);
        auto* preview = new MiiPreview(&pending[i].mii, 160);
        preview->setDimensions(200, 250);
        card->addView(preview);
        const int index = (int)i;
        // Cross: leave it out (or back in); Square: delete its file (or not)
        card->registerClickAction([this, index](brls::View*) {
            setState(index, states[index] == LEAVE ? IMPORT : LEAVE);
            return true;
        });
        card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
        card->registerAction("import/delete"_i18n, brls::BUTTON_X, [this, index](brls::View*) {
            setState(index, states[index] == DELETE ? IMPORT : DELETE);
            return true;
        });
        cards.push_back(card);
        row->addView(card);
    }
    // up / down to the card straight above or below
    const int n = (int)cards.size();
    for (int i = 0; i < n; i++) {
        if (i >= kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::UP, cards[i - kColumns]);
        if (i / kColumns < (n - 1) / kColumns)
            cards[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, cards[std::min(i + kColumns, n - 1)]);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setContentView(content);
    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("import/title"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    // the START hint (with the count) is on the frame; it changes as cards do
    rootFrame = frame;
    refreshHints();
    return frame;
}

void ImportActivity::setState(int index, State s)
{
    states[index] = s;
    static_cast<ImportCard*>(cards[index])->state = s;
    refreshHints();
}

// The summary line, each card's Cross / Square words, START's count.
void ImportActivity::refreshHints()
{
    int in = 0, del = 0;
    for (State s : states) {
        in += s == IMPORT ? 1 : 0;
        del += s == DELETE ? 1 : 0;
    }
    std::string text = brls::getStr("import/found", std::to_string(pending.size()));
    if (unreadable)
        text += " " + brls::getStr("import/unreadable", std::to_string(unreadable));
    summary->setText(text);
    for (size_t i = 0; i < cards.size(); i++) {
        cards[i]->updateActionHint(brls::BUTTON_A, states[i] == LEAVE ? "import/include"_i18n : "import/leave"_i18n);
        cards[i]->updateActionHint(brls::BUTTON_X, states[i] == DELETE ? "import/keep"_i18n : "import/delete"_i18n);
    }
    // START: re-registered in place (see LibraryActivity::settingsAction)
    rootFrame->registerAction(del ? brls::getStr("import/go_delete", std::to_string(in), std::to_string(del))
                                  : brls::getStr("import/go", std::to_string(in)),
        brls::BUTTON_START, [this](brls::View*) {
            brls::sync([this] { finish(); });
            return true;
        });
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

// START: delete the files marked (after asking), import the rest, back to
// the Mii list.
void ImportActivity::finish()
{
    std::vector<MiiLibrary::Pending> chosen;
    std::vector<std::string> toDelete;
    for (size_t i = 0; i < pending.size(); i++) {
        if (states[i] == IMPORT)
            chosen.push_back(pending[i]);
        else if (states[i] == DELETE)
            toDelete.push_back(pending[i].file);
    }
    auto go = [this, chosen, toDelete] {
        MiiLibrary& lib = MiiLibrary::instance();
        int deleted = 0;
        for (const std::string& f : toDelete)
            deleted += lib.deleteImport(f) ? 1 : 0;
        const MiiLibrary::ImportResult r = lib.importPending(chosen);
        std::string msg = std::to_string(r.imported) + " " + "library/import_done"_i18n;
        if (deleted)
            msg += ", " + brls::getStr("import/deleted_n", std::to_string(deleted));
        if (r.full)
            msg += ". " + "library/import_full"_i18n;
        brls::Application::notify(msg);
        auto cb = onDone;
        const int imported = r.imported;
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb, imported] {
            if (cb)
                cb(imported);
        });
    };
    if (toDelete.empty()) {
        go();
        return;
    }
    auto* dialog = new brls::Dialog(brls::getStr("import/delete_confirm", std::to_string(toDelete.size())));
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("import/delete"_i18n, [go] { brls::sync(go); });
    dialog->open();
}
