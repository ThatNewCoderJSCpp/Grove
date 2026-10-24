#ifndef GROVE_RUNTIME_RUNTIME_HPP
#define GROVE_RUNTIME_RUNTIME_HPP

#include "../project.hpp"
#include "../script/builder.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__ANDROID__)
namespace fizmo {
namespace android {
class App;
} // namespace android
} // namespace fizmo
#endif

namespace grove {
namespace runtime {

struct RunOptions {
    std::string window_id;
    double max_fps = 60.0;
    double idle_fps = 12.0;
    std::optional<bool> vsync;
    bool scripts = true;
    ScriptTools tools;
    const ScriptBuild* prebuilt = nullptr;
};

int run(Project& project, const RunOptions& options = RunOptions());

#if defined(__ANDROID__)
int run_android(Project& project, fizmo::android::App& app, const ScriptBuild* scripts, const RunOptions& options = RunOptions());
#endif

std::vector<std::string> font_families(std::string_view kind = "all");

} // namespace runtime
} // namespace grove

#endif // GROVE_RUNTIME_RUNTIME_HPP
