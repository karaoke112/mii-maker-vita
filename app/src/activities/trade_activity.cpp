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
    TradeView(bool receiving, const std::vector<mii_charinfo_t>* miis, mii_charinfo_t* offered, brls::Label* count,
        brls::Label* status, brls::Box* list, std::function<void()> onSaved)
        : receiving(receiving)
        , miis(miis)
        , offeredMii(offered)
        , count(count)
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
        if (offerPending && !session->sending()) {
            offerPending = false;
            offerNext();
        }
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
                if (session->sending() || next > 0)
                    return true;
                target = name;
                targetId = id;
                accepted = 0;
                offerNext();
                return true;
            });
            list->addView(b);
        }
        if (!now.empty() && !session->sending())
            brls::Application::giveFocus(list->getChildren()[0]);
        if (!session->sending())
            status->setText(now.empty() ? "trade/looking"_i18n : "trade/choose"_i18n);
    }

    // the chosen Miis go one at a time; the receiver is asked about each
    void offerNext()
    {
        const int n = (int)miis->size();
        *offeredMii = (*miis)[next];
        if (count)
            count->setText(n > 1 ? brls::getStr("trade/of", std::to_string(next + 1), std::to_string(n)) : "");
        session->send(targetId, (*miis)[next]);
        status->setText("trade/offering"_i18n + " " + target + "...");
        next++;
    }

    void answered(bool ok, bool timedOut)
    {
        const int n = (int)miis->size();
        if (ok)
            accepted++;
        if (timedOut && !ok) {
            // no answer: stop here (the others are not offered)
            status->setText("trade/no_answer"_i18n);
            next = 0;
            return;
        }
        if (next < n) {
            offerPending = true; // from tick(), outside the session's callback
            return;
        }
        // all offered: say how many were taken
        next = 0;
        if (n == 1 && !ok) {
            status->setText(target + " " + "trade/declined"_i18n);
            return;
        }
        brls::Application::notify(n == 1 ? target + " " + "trade/accepted"_i18n
                                         : brls::getStr("trade/accepted_n", target, std::to_string(accepted), std::to_string(n)));
        brls::sync([] { brls::Application::popActivity(brls::TransitionAnimation::NONE); });
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
    const std::vector<mii_charinfo_t>* miis;
    mii_charinfo_t* offeredMii; // the preview shows it
    brls::Label* count;
    brls::Label* status;
    brls::Box* list;
    std::function<void()> onSaved;

    trade::Link link;
    std::unique_ptr<trade::Session> session;
    bool started = false, reportedFailure = false;
    std::vector<std::pair<uint32_t, std::string>> shown;
    std::string target;
    uint32_t targetId = 0;
    int next = 0;     // the next Mii to offer (0: not sending)
    int accepted = 0; // taken so far
    bool offerPending = false;
    bool asking = false;
    mii_charinfo_t incoming {};
};

} // namespace

TradeActivity::TradeActivity(std::function<void()> onSaved)
    : receiving(true)
    , onSaved(std::move(onSaved))
{
}

TradeActivity::TradeActivity(std::vector<mii_charinfo_t> miis)
    : receiving(false)
    , miis(std::move(miis))
{
    if (!this->miis.empty())
        shown = this->miis[0];
}

brls::View* TradeActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);
    root->setPadding(30, 60, 30, 60);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1);

    if (!receiving) {
        auto* col = new brls::Box(brls::Axis::COLUMN);
        col->setAlignItems(brls::AlignItems::CENTER);
        col->setMarginRight(60);
        auto* preview = new MiiPreview(&shown, 256);
        preview->setDimensions(260, 330);
        col->addView(preview);
        count = new brls::Label();
        count->setFontSize(20);
        count->setTextColor(ui::textDim());
        count->setText(miis.size() > 1 ? brls::getStr("trade/count", std::to_string(miis.size())) : "");
        col->addView(count);
        root->addView(col);
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

    auto* ticker = new TradeView(receiving, &miis, &shown, count, status, list, onSaved);
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
