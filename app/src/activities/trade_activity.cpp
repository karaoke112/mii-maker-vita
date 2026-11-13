#include "activities/trade_activity.hpp"

#include "mii_library.hpp"
#include "trade.hpp"
#include "ui_theme.hpp"
#include "views/mii_preview.hpp"

#include <memory>

using namespace brls::literals;

namespace {

// Runs the link and the session from the frame loop and keeps the screen
// up to date. Nothing in it is drawn.
class TradeView : public brls::View
{
  public:
    TradeView(bool receiving, const mii_charinfo_t* mii, brls::Label* status, brls::Box* list,
        std::function<void()> onSaved)
        : receiving(receiving)
        , mii(mii)
        , status(status)
        , list(list)
        , onSaved(std::move(onSaved))
    {
        this->setFocusable(false);
    }

    ~TradeView() override
    {
        session.reset(); // closes the socket before leaving the network
        link.stop();
    }

    void draw(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext*) override { tick(); }

  private:
    void tick()
    {
        const uint64_t now = brls::getCPUTimeUsec();
        switch (link.state()) {
        case trade::Link::State::IDLE:
            if (!started) {
                started = true;
                status->setText("trade/joining"_i18n);
                link.start();
            }
            return;
        case trade::Link::State::JOINING:
            link.poll();
            return;
        case trade::Link::State::FAILED:
            if (!reportedFailure) {
                reportedFailure = true;
                status->setText("trade/not_joined"_i18n);
            }
            return;
        case trade::Link::State::CONNECTED:
            break;
        }
        if (!session) {
            std::string error;
            std::unique_ptr<trade::Transport> t = link.open(&error);
            if (!t) {
                status->setText("trade/no_socket"_i18n + " (" + error + ")");
                link.stop();
                reportedFailure = true;
                return;
            }
            session.reset(new trade::Session(std::move(t), receiving, trade::userName()));
            session->onOffer = [this](const trade::Session::Peer& from, uint32_t offer, const mii_charinfo_t& m) {
                offered(from, offer, m);
            };
            session->onAnswer = [this](bool accepted, bool timedOut) { answered(accepted, timedOut); };
            status->setText(receiving ? "trade/waiting"_i18n + " " + trade::userName() : "trade/looking"_i18n);
        }
        session->update(now);
        if (!receiving)
            showReceivers();
    }

    // ---- sending ----

    void showReceivers()
    {
        std::vector<std::pair<uint32_t, std::string>> now;
        for (const auto& p : session->receivers())
            now.push_back({ p.id, p.name });
        if (now == shown)
            return;
        shown = now;
        // the buttons are about to go: keep focus off them
        brls::Application::giveFocus(this->getParent());
        list->clearViews();
        for (const auto& p : now) {
            auto* b = new brls::Button();
            b->setText(p.second);
            b->setWidth(420);
            b->setMarginBottom(12);
            const uint32_t id = p.first;
            const std::string name = p.second;
            b->registerClickAction([this, id, name](brls::View*) {
                if (session->sending())
                    return true;
                target = name;
                session->send(id, *mii);
                status->setText("trade/offering"_i18n + " " + name + "...");
                return true;
            });
            list->addView(b);
        }
        if (!now.empty() && !session->sending())
            brls::Application::giveFocus(list->getChildren()[0]);
        if (!session->sending())
            status->setText(now.empty() ? "trade/looking"_i18n : "trade/choose"_i18n);
    }

    void answered(bool accepted, bool timedOut)
    {
        if (accepted) {
            brls::Application::notify(target + " " + "trade/accepted"_i18n);
            brls::sync([] { brls::Application::popActivity(brls::TransitionAnimation::NONE); });
            return;
        }
        status->setText(timedOut ? "trade/no_answer"_i18n : target + " " + "trade/declined"_i18n);
    }

    // ---- receiving ----

    void offered(const trade::Session::Peer& from, uint32_t offer, const mii_charinfo_t& m)
    {
        if (asking) { // one question at a time
            session->answer(from.id, offer, false);
            return;
        }
        asking = true;
        incoming = m;
        char name[64];
        mii_name_get_utf8(&incoming, name, sizeof(name));

        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setPadding(20, 30, 20, 30);
        auto* preview = new MiiPreview(&incoming, 192);
        preview->setDimensions(200, 240);
        box->addView(preview);
        auto* text = new brls::Label();
        text->setText(from.name + " " + "trade/wants_to_send"_i18n + " " + name + ".");
        text->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        text->setMarginTop(14);
        box->addView(text);

        auto* dialog = new brls::Dialog(box);
        const uint32_t to = from.id;
        dialog->addButton("trade/decline"_i18n, [this, to, offer] {
            session->answer(to, offer, false);
            asking = false;
        });
        dialog->addButton("trade/accept"_i18n, [this, to, offer] {
            mii_charinfo_t save = incoming;
            char n[64];
            mii_name_get_utf8(&save, n, sizeof(n));
            const bool ok = MiiLibrary::instance().put(&save) >= 0;
            session->answer(to, offer, ok);
            brls::Application::notify(ok ? "trade/received"_i18n + " " + n : "library/import_full"_i18n);
            if (ok && onSaved)
                onSaved();
            asking = false;
        });
        dialog->setCancelable(false);
        // The dialog returns focus to whatever had it when it opened: make
        // that this screen (not the library under it), and open it outside
        // of drawing.
        brls::Application::giveFocus(this->getParent());
        brls::sync([dialog] { dialog->open(); });
    }

    bool receiving;
    const mii_charinfo_t* mii;
    brls::Label* status;
    brls::Box* list;
    std::function<void()> onSaved;

    trade::Link link;
    std::unique_ptr<trade::Session> session;
    bool started = false, reportedFailure = false;
    std::vector<std::pair<uint32_t, std::string>> shown;
    std::string target;
    bool asking = false;
    mii_charinfo_t incoming {};
};

} // namespace

TradeActivity::TradeActivity(std::function<void()> onSaved)
    : receiving(true)
    , onSaved(std::move(onSaved))
{
}

TradeActivity::TradeActivity(const mii_charinfo_t& mii)
    : receiving(false)
    , mii(mii)
{
}

brls::View* TradeActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);
    root->setPadding(30, 60, 30, 60);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1);

    if (!receiving) {
        auto* preview = new MiiPreview(&mii, 256);
        preview->setDimensions(260, 330);
        preview->setMarginRight(60);
        root->addView(preview);
    }

    auto* right = new brls::Box(brls::Axis::COLUMN);
    right->setGrow(1);
    auto* status = new brls::Label();
    status->setFontSize(24);
    status->setMarginBottom(24);
    right->addView(status);
    auto* list = new brls::Box(brls::Axis::COLUMN);
    right->addView(list);
    auto* note = new brls::Label();
    note->setFontSize(18);
    note->setTextColor(ui::textDim());
    note->setMarginTop(24);
    note->setText(receiving ? "trade/receive_note"_i18n : "trade/send_note"_i18n);
    right->addView(note);
    root->addView(right);

    auto* ticker = new TradeView(receiving, &mii, status, list, onSaved);
    ticker->setDimensions(1, 1);
    root->addView(ticker);

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle(receiving ? "library/add_receive"_i18n : "library/send_vita"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    root->setFocusable(true);
    root->setHideHighlight(true);
    return frame;
}
