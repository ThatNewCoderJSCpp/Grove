#ifndef GROVE_RUNTIME_RUNTIME_HPP
#define GROVE_RUNTIME_RUNTIME_HPP

#include "../project.hpp"
#include "../script/builder.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace grove {
namespace runtime {

struct RunOptions {
    std::string window_id;
    double max_fps = 60.0;
    bool scripts = true;
    ScriptTools tools;
    const ScriptBuild* prebuilt = nullptr;
};

int run(Project& project, const RunOptions& options = RunOptions());

std::vector<std::string> font_families(std::string_view kind = "all");

} // namespace runtime
} // namespace grove

#endif // GROVE_RUNTIME_RUNTIME_HPP
