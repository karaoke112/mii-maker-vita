#include <borealis.hpp>

#include <cstdlib>
#include <cstring>

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
    ui::applyTheme(Settings::get().theme);

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
    ui::applyVariant();
    // START opens settings in the library and saves in the editor; the app is
    // closed with the PS button.
    brls::Application::setGlobalQuit(false);

    MiiLibrary::instance().load();
    // 3D heads need a face resource the user provides; without it the
    // previews say where to put it.
    HeadRenderer::instance().init(paths::datDir(), paths::cacheDir());

    brls::Application::pushActivity(new LibraryActivity());
    while (brls::Application::mainLoop())
        ;
    return EXIT_SUCCESS;
}
