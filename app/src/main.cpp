#include <borealis.hpp>

#include <cstdlib>
#include <cstring>

#include "mii_qr.hpp"
#include "music.hpp"
#include "activities/library_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "paths.hpp"
#include "settings.hpp"
#include "ui_theme.hpp"

using namespace brls::literals;

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }

    paths::prepare();
    Settings::get().load();
    qr::loadKeys();
    ui::applyTheme(Settings::get().theme);

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
    ui::applyVariant();
    // The UI font is Poppins (font/font.ttf); characters it lacks, such as
    // Japanese in Mii names, come from M PLUS Rounded 1c.
    if (brls::Application::loadFontFromFile("mplus", std::string(BRLS_RESOURCES) + "font/mplus-rounded-1c.ttf"))
        nvgAddFallbackFontId(brls::Application::getNVGContext(), brls::Application::getFont(brls::FONT_REGULAR),
            brls::Application::getFont("mplus"));
    // START opens settings in the library and saves in the editor; the app is
    // closed with the PS button.
    brls::Application::setGlobalQuit(false);

    MiiLibrary::instance().load();
    // 3D heads need a face resource the user provides; without it the
    // previews say where to put it.
    HeadRenderer::instance().init(paths::datDir(), paths::iconCache(), paths::appDir() + "/gpu.log", Settings::get().gpu);
    music::Player::instance().play((music::Track)Settings::get().music);

    // no fade-in: the first frame follows the boot splash directly
    brls::Application::pushActivity(new LibraryActivity(), brls::TransitionAnimation::NONE);
    while (brls::Application::mainLoop())
        HeadRenderer::instance().pumpGpu(); // GPU previews, between frames
    return EXIT_SUCCESS;
}
