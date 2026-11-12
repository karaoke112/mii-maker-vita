#include <borealis.hpp>

#include <cstdlib>
#include <cstring>

#include "activities/library_activity.hpp"
#include "head_renderer.hpp"
#include "mii_library.hpp"
#include "paths.hpp"
#include "ui_theme.hpp"

using namespace brls::literals;

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }

    ui::applyTheme();

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
    // START means "save" in the editor, so only the library binds it to quit
    // (global quit would override the editor's binding).
    brls::Application::setGlobalQuit(false);

    paths::prepare();
    MiiLibrary::instance().load();
    // 3D heads need a face resource the user provides; without it the
    // previews say where to put it.
    HeadRenderer::instance().init(paths::datDir());

    brls::Application::pushActivity(new LibraryActivity());
    while (brls::Application::mainLoop())
        ;
    return EXIT_SUCCESS;
}
