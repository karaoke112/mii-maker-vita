#include "activities/qr_activity.hpp"

#include "mii_library.hpp"
#include "mii_qr.hpp"
#include "ui_theme.hpp"
#include "views/mii_preview.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__PSV__)
#include <psp2/camera.h>
#include <psp2/kernel/sysmem.h>
#endif

using namespace brls::literals;

// ---- showing a code ------------------------------------------------------

// A QR code, black on a white card with the quiet zone around it.
class QrView : public brls::View
{
  public:
    explicit QrView(qr::Code code)
        : code(std::move(code))
    {
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 10);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        if (code.size <= 0)
            return;
        // whole pixels per module, so every module is the same size
        const int quiet = 4;
        const float side = std::fmin(width, height);
        const float m = std::floor(side / (code.size + 2 * quiet));
        const float ox = std::round(x + (width - m * code.size) / 2), oy = std::round(y + (height - m * code.size) / 2);
        nvgBeginPath(vg);
        for (int r = 0; r < code.size; r++)
            for (int c = 0; c < code.size; c++)
                if (code.dark(c, r))
                    nvgRect(vg, ox + c * m, oy + r * m, m, m);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);
    }

  private:
    qr::Code code;
};

brls::View* QrShowActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1);

    qr::Code code;
    const bool made = qr::make(mii, &code);

    auto* preview = new MiiPreview(&mii, 256);
    preview->setDimensions(260, 330);
    preview->setMarginRight(60);
    root->addView(preview);

    auto* right = new brls::Box(brls::Axis::COLUMN);
    right->setAlignItems(brls::AlignItems::CENTER);
    auto* view = new QrView(code);
    view->setDimensions(400, 400);
    right->addView(view);
    auto* note = new brls::Label();
    note->setFontSize(18);
    note->setTextColor(ui::textDim());
    note->setMarginTop(16);
    note->setWidth(520);
    note->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    if (!made) {
        note->setText("qr/make_failed"_i18n);
    } else {
        MiiLibrary& lib = MiiLibrary::instance();
        const std::string path = lib.qrDir() + "/" + MiiLibrary::fileName(mii) + ".png";
        note->setText(qr::savePng(code, path) ? "qr/saved"_i18n + " " + path : "qr/save_failed"_i18n);
    }
    right->addView(note);
    root->addView(right);

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle("qr/title"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    // nothing on this screen takes focus; let the frame hold it
    root->setFocusable(true);
    root->setHideHighlight(true);
    return frame;
}

// ---- scanning ------------------------------------------------------------

namespace {

// Frames from the back camera, read and searched on a thread of their own.
// The newest frame is kept for the preview.
class CameraScanner
{
  public:
    static constexpr int W = 640, H = 480;

    ~CameraScanner() { stop(); }

    // false: no camera (desktop builds) or it would not start
    bool start()
    {
#if defined(__PSV__)
        running = true;
        worker = std::thread([this] { run(); });
        return true;
#else
        return false;
#endif
    }

    void stop()
    {
        running = false;
        if (worker.joinable())
            worker.join();
    }

    // Copies the newest frame (RGBA, W x H) if there is a new one.
    bool takeFrame(std::vector<uint8_t>* out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!fresh)
            return false;
        out->swap(frame);
        fresh = false;
        return true;
    }

    bool found(mii_charinfo_t* mii)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hasResult)
            return false;
        *mii = result;
        return true;
    }

    bool sawOtherCode() const { return otherCode; }
    bool failed() const { return error; }

  private:
#if defined(__PSV__)
    void run()
    {
        SceUID block = sceKernelAllocMemBlock("camera", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * 1024 * 5, nullptr);
        void* base = nullptr;
        if (block < 0 || sceKernelGetMemBlockBase(block, &base) < 0) {
            error = true;
            return;
        }
        SceCameraInfo info;
        std::memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        info.format = SCE_CAMERA_FORMAT_ABGR;
        info.resolution = SCE_CAMERA_RESOLUTION_640_480;
        info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
        info.sizeIBase = 4 * W * H;
        info.pIBase = base;
        info.pitch = 0;
        const int device = SCE_CAMERA_DEVICE_BACK;
        if (sceCameraOpen(device, &info) < 0 || sceCameraStart(device) < 0) {
            sceCameraClose(device);
            sceKernelFreeMemBlock(block);
            error = true;
            return;
        }
        qr::Scanner scanner;
        int frames = 0;
        std::vector<uint8_t> rgba((size_t)W * H * 4);
        while (running) {
            SceCameraRead read;
            std::memset(&read, 0, sizeof(read));
            read.size = sizeof(read);
            read.mode = 0; // wait for the next frame
            if (sceCameraRead(device, &read) < 0)
                continue;
            // ABGR words are R, G, B, A bytes: RGBA as NanoVG wants it
            const uint8_t* src = static_cast<const uint8_t*>(base);
            std::memcpy(rgba.data(), src, rgba.size());
            for (size_t i = 3; i < rgba.size(); i += 4)
                rgba[i] = 255;
            mii_charinfo_t mii;
            const bool hit = scanner.scan(W, H, [&](uint8_t* gray) {
                const uint8_t* p = rgba.data();
                for (int i = 0; i < W * H; i++, p += 4)
                    gray[i] = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            }, &mii, frames++);
            otherCode = scanner.sawOtherCode();
            {
                std::lock_guard<std::mutex> lock(mutex);
                frame.swap(rgba);
                if (rgba.size() != frame.size())
                    rgba.resize(frame.size());
                fresh = true;
                if (hit) {
                    result = mii;
                    hasResult = true;
                }
            }
            if (hit)
                break;
        }
        sceCameraStop(device);
        sceCameraClose(device);
        sceKernelFreeMemBlock(block);
    }
#endif

    std::thread worker;
    std::atomic<bool> running { false };
    std::atomic<bool> otherCode { false };
    std::atomic<bool> error { false };
    std::mutex mutex;
    std::vector<uint8_t> frame;
    bool fresh = false;
    mii_charinfo_t result;
    bool hasResult = false;
};

// The camera picture with a frame to aim with; reports the first Mii.
class CameraView : public brls::View
{
  public:
    CameraView(brls::Label* status, std::function<void(const mii_charinfo_t&)> onFound)
        : status(status)
        , onFound(std::move(onFound))
    {
        available = camera.start();
        if (!available)
            status->setText("qr/no_camera"_i18n + " " + MiiLibrary::instance().importDir() + "/ " + "qr/no_camera_then"_i18n);
    }

    ~CameraView() override
    {
        camera.stop();
        if (image >= 0)
            nvgDeleteImage(brls::Application::getNVGContext(), image);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 10);
        nvgFillColor(vg, nvgRGB(20, 20, 22));
        nvgFill(vg);

        if (camera.takeFrame(&pixels)) {
            if (image < 0)
                image = nvgCreateImageRGBA(vg, CameraScanner::W, CameraScanner::H, 0, pixels.data());
            else
                nvgUpdateImage(vg, image, pixels.data());
        }
        if (image >= 0) {
            // fit the 4:3 picture
            float w = width, h = width * 3 / 4;
            if (h > height) {
                h = height;
                w = height * 4 / 3;
            }
            const float ix = x + (width - w) / 2, iy = y + (height - h) / 2;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, ix, iy, w, h, 10);
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, w, h, 0, image, 1.0f));
            nvgFill(vg);
            // aiming square
            const float s = h * 0.7f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + (width - s) / 2, y + (height - s) / 2, s, s, 12);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 220));
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
        }

        if (available) {
            if (camera.failed() && !reportedError) {
                status->setText("qr/camera_failed"_i18n);
                reportedError = true;
            } else if (camera.sawOtherCode() != showingOther) {
                showingOther = camera.sawOtherCode();
                status->setText(showingOther ? "qr/not_a_mii"_i18n : "qr/aim"_i18n);
            }
            mii_charinfo_t mii;
            if (!done && camera.found(&mii)) {
                done = true;
                auto cb = onFound;
                brls::sync([cb, mii] { cb(mii); });
            }
        }
    }

  private:
    CameraScanner camera;
    brls::Label* status;
    std::function<void(const mii_charinfo_t&)> onFound;
    std::vector<uint8_t> pixels;
    int image = -1;
    bool available = false;
    bool done = false;
    bool showingOther = false;
    bool reportedError = false;
};

} // namespace

brls::View* QrScanActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);
    root->setPadding(20, 40, 20, 40);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1);

    auto* status = new brls::Label();
    status->setFontSize(22);
    status->setText("qr/aim"_i18n);

    OnFound found = onFound;
    auto* camera = new CameraView(status, [found](const mii_charinfo_t& mii) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [found, mii] {
            if (found)
                found(mii);
        });
    });
    camera->setDimensions(720, 540);
    root->addView(camera);

    auto* side = new brls::Box(brls::Axis::COLUMN);
    side->setGrow(1);
    side->setMarginLeft(30);
    side->addView(status);
    auto* hint = new brls::Label();
    hint->setFontSize(18);
    hint->setTextColor(ui::textDim());
    hint->setMarginTop(18);
    hint->setText("qr/formats"_i18n);
    side->addView(hint);
    root->addView(side);

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle("qr/scan_title"_i18n);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    root->setFocusable(true);
    root->setHideHighlight(true);
    return frame;
}
