#include <borealis.hpp>

#include <cstdlib>
#include <cstring>

#include "activities/library_activity.hpp"
#include "mii_library.hpp"

using namespace brls::literals;

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }

    // A narrower sidebar leaves room for the preview on the Vita's screen.
    brls::getStyle().addMetric("brls/tab_frame/sidebar_width", 250.0f);
    brls::getStyle().addMetric("brls/sidebar/padding_left", 30.0f);
    brls::getStyle().addMetric("brls/sidebar/padding_right", 20.0f);

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
    // START means "save" in the editor, so only the library binds it to quit
    // (global quit would override the editor's binding).
    brls::Application::setGlobalQuit(false);

    MiiLibrary::instance().load();

    brls::Application::pushActivity(new LibraryActivity());
    while (brls::Application::mainLoop())
        ;
    return EXIT_SUCCESS;
}
