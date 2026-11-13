#include "activities/photo_activity.hpp"

#include "activities/editor_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "mii_qr.hpp"
#include "paths.hpp"
#include "ui_theme.hpp"
#include "views/mii_preview.hpp"
#include "views/popup_menu.hpp"

#include <mii3d/mii3d.h>

#include <cerrno>
#include <ctime>
#include <memory>
#include <sys/stat.h>
#include <thread>

using namespace brls::literals;

namespace {

// The backgrounds: a name and 0xAARRGGBB (alpha 0: a clear PNG, shown on
// light gray here)
struct Background {
    const char* key;
    uint32_t argb;
};
const Background kBackgrounds[] = {
    { "photo/bg_white", 0xFFFFFFFFu },  { "photo/bg_gray", 0xFFE6E6E6u },    { "photo/bg_sky", 0xFFBFE3FFu },
    { "photo/bg_mint", 0xFFC8F0DCu },   { "photo/bg_yellow", 0xFFFFF2B3u },  { "photo/bg_pink", 0xFFFFD3E2u },
    { "photo/bg_lavender", 0xFFE0D6FAu }, { "photo/bg_night", 0xFF22283Au }, { "photo/bg_clear", 0x00000000u },
};
constexpr int kBackgroundCount = (int)(sizeof(kBackgrounds) / sizeof(kBackgrounds[0]));

uint32_t shownColor(int i)
{
    const uint32_t c = kBackgrounds[i].argb;
    return (c >> 24) ? (c & 0xFFFFFFu) : 0xD8D8D8u;
}

NVGcolor nvg(uint32_t rgb) { return nvgRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

void makeDir(const std::string& path)
{
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        brls::Logger::warning("mkdir {} failed", path);
}

} // namespace

PhotoActivity::PhotoActivity(const mii_charinfo_t& mii, bool favorite, int* expression, int* pose)
    : mii(mii)
    , favorite(favorite)
    , expression(expression)
    , pose(pose)
{
}

brls::View* PhotoActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);

    preview = new MiiPreview(&mii, 384);
    preview->setStage(true);
    preview->setFraming(MiiPreview::Framing::FULL_BODY);
    preview->setYawSource(&yaw);
    preview->setPitchSource(&pitch);
    preview->setFrontSource(&facingFront);
    preview->setExpressionSource(expression);
    preview->setPoseSource(pose);
    preview->setFavoriteSource(&favorite);
    preview->setBackdrop(nvg(shownColor(background)));
    // the photo's own shape (960 x 544) on its background: the Mii is framed
    // here as it will be in the picture
    preview->setDimensions(kStageW, kStageH);
    stageBox = new brls::Box();
    stageBox->setDimensions(kStageW, kStageH);
    stageBox->setBackgroundColor(nvg(shownColor(background)));
    stageBox->setCornerRadius(6);
    stageBox->addView(preview);
    auto* left = new brls::Box(brls::Axis::COLUMN);
    left->setWidth(kStageW + 40);
    left->setGrow(0);
    left->setJustifyContent(brls::JustifyContent::CENTER);
    left->setAlignItems(brls::AlignItems::CENTER);
    left->addView(stageBox);
    root->addView(left);

    auto* right = new brls::Box(brls::Axis::COLUMN);
    right->setGrow(1);
    right->setJustifyContent(brls::JustifyContent::CENTER);
    right->setPadding(0, 40, 0, 10);
    auto* title = new brls::Label();
    title->setText("photo/title"_i18n);
    title->setFontSize(36);
    title->setMarginBottom(24);
    right->addView(title);

    std::vector<PopupMenu::Item> items;
    PopupMenu::Item poseItem = PopupMenu::item("view/pose"_i18n, [this] { showPoseMenu(pose); });
    poseItem.value = [this] { return poseDisplayName(*pose); };
    items.push_back(poseItem);
    PopupMenu::Item expr = PopupMenu::item("view/expression"_i18n, [this] { showExpressionMenu(expression); });
    expr.value = [this] { return brls::getStr("expressions/names/" + std::to_string(*expression)); };
    items.push_back(expr);
    PopupMenu::Item bg = PopupMenu::item("photo/background"_i18n, [this] {
        std::vector<std::string> names;
        for (const Background& b : kBackgrounds)
            names.push_back(brls::getStr(b.key));
        auto* dropdown = new brls::Dropdown("photo/background"_i18n, names, [this](int i) {
            if (i < 0)
                return;
            background = i;
            preview->setBackdrop(nvg(shownColor(i)));
            stageBox->setBackgroundColor(nvg(shownColor(i)));
        },
            background);
        brls::Application::pushActivity(new brls::Activity(dropdown));
    });
    bg.value = [this] { return brls::getStr(kBackgrounds[background].key); };
    bg.swatch = [this] { return shownColor(background); };
    items.push_back(bg);
    PopupMenu::Item framing = PopupMenu::item("photo/framing"_i18n, [this] {
        closeUp = !closeUp;
        preview->setFraming(closeUp ? MiiPreview::Framing::BUST : MiiPreview::Framing::FULL_BODY);
    });
    framing.value = [this] { return closeUp ? "photo/close_up"_i18n : "photo/whole"_i18n; };
    items.push_back(framing);
    items.push_back(PopupMenu::item("photo/take"_i18n, [this] { takePhoto(); }));
    for (size_t i = 0; i < items.size(); i++)
        right->addView(PopupMenu::row(items[i], i == 0));
    right->setDefaultFocusedIndex(1);
    root->addView(right);

    auto* frame = new brls::AppletFrame(root);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->registerAction("editor/rotate"_i18n, brls::BUTTON_RSB, [](brls::View*) { return true; });
    frame->registerAction("editor/reset_position"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        facingFront = true;
        return true;
    });
    frame->registerAction("photo/take"_i18n, brls::BUTTON_X, [this](brls::View*) {
        takePhoto();
        return true;
    });
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    return frame;
}

// The picture is drawn on a thread of its own (a second or two on the
// Vita), then saved; a notice says where.
void PhotoActivity::takePhoto()
{
    if (*busy)
        return;
    *busy = true;
    HeadRenderer::Spec spec;
    spec.mii = mii;
    spec.yaw = preview->getYaw();
    spec.pitch = pitch;
    preview->currentFraming(&spec.fullBody, &spec.zoom, &spec.centerY);
    spec.expression = *expression;
    spec.pose = *pose;
    spec.pants = favorite ? MII3D_PANTS_RED : MII3D_PANTS_GRAY;
    const uint32_t bg = kBackgrounds[background].argb;

    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    const std::string dir = paths::photoDir();
    const std::string path = dir + "/" + MiiLibrary::fileName(mii) + "_" + stamp + ".png";
    brls::Application::notify("photo/taking"_i18n);

    // the Vita's screen size: the Mii as framed here, more room either side
    std::shared_ptr<bool> flag = busy;
    std::thread([flag, spec, bg, dir, path] {
        auto pixels = std::make_shared<std::vector<uint8_t>>();
        bool ok = HeadRenderer::instance().renderPhoto(spec, 960, 544, bg, pixels.get());
        if (ok) {
            makeDir(dir.substr(0, dir.rfind('/')));
            makeDir(dir);
            ok = qr::saveRgbaPng(path, 960, 544, pixels->data());
        }
        brls::sync([flag, ok, path] {
            *flag = false;
            brls::Application::notify(ok ? "photo/saved"_i18n + " " + path : "photo/failed"_i18n);
        });
    }).detach();
}
