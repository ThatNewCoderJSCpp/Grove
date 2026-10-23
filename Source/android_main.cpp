#include "grove/app.hpp"
#include "grove/bundle.hpp"
#include "grove/runtime/runtime.hpp"

#if defined(__ANDROID__)

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <iostream>
#include <string>

namespace {

int show_error(fizmo::android::App& app, const std::string& message) {
    fizmo::android::log(fizmo::android::LogLevel::Error, "grove", message);
    fizmo::graphics::Renderer renderer(app.surface());
    renderer.bind();
    fizmo::android::Event e;
    while (app.running()) {
        bool redraw = false;
        while (app.poll(e, 250)) {
            if (e.type == fizmo::android::EventType::Back) app.finish();
            redraw = true;
        }
        if (!redraw || !renderer.is_bound()) continue;
        const float k = app.density();
        const fizmo::android::Insets in = app.content_insets();
        const int x = in.left + static_cast<int>(24 * k);
        const int y = in.top + static_cast<int>(32 * k);
        const unsigned w = app.surface().width() > static_cast<unsigned>(x * 2) ? app.surface().width() - static_cast<unsigned>(x * 2) : app.surface().width();
        renderer.begin_frame();
        renderer.clear(fizmo::colors::Color(30, 30, 36));
        renderer.draw_text(x, y, w, static_cast<unsigned>(60 * k), std::string("This app couldn't start"), fizmo::text::TextStyle(22.0 * k, fizmo::colors::Color(243, 139, 168)));
        renderer.draw_text(x, y + static_cast<int>(48 * k), w, app.surface().height(), message, fizmo::text::TextStyle(15.0 * k, fizmo::colors::Color(205, 214, 244)));
        renderer.present();
    }
    return 1;
}

} // namespace

int fizmo_android_main(fizmo::android::App& app) {
    const std::optional<std::string> data = app.assets().read("grove/app.bundle");
    if (!data) return show_error(app, "this APK has no grove/app.bundle asset; build it again with grove build");
    std::optional<grove::Bundle> bundle = grove::parse_bundle(*data);
    if (!bundle) return show_error(app, "grove/app.bundle is damaged; build the app again");

    grove::AppLoadOptions options;
    options.root = app.internal_data_path().empty() ? std::string("/data/local/tmp") : app.internal_data_path();
    options.cache_dir = app.cache_path();
    options.module_dir = app.native_library_dir();

    grove::LoadedApp loaded;
    std::string error;
    if (!grove::load_app(*bundle, loaded, error, options)) return show_error(app, error);

    grove::runtime::RunOptions run;
    run.window_id = loaded.window_id;
    run.scripts = true;
    return grove::runtime::run_android(loaded.project, app, &loaded.scripts, run);
}

#endif // __ANDROID__
