#ifndef GROVE_APP_HPP
#define GROVE_APP_HPP

#include "bundle.hpp"
#include "project.hpp"
#include "script/builder.hpp"

#include <ostream>
#include <string>

namespace grove {

struct AppBuildOptions {
    std::string output;
    std::string window_id;
    std::string stub;
    ScriptTools tools;
    bool portable = true;
};

struct AppBuildResult {
    bool ok = false;
    std::string output;
    std::size_t files = 0;
    std::size_t modules = 0;
    std::uint64_t bytes = 0;
};

std::string default_app_output(const std::string& document_path);
AppBuildResult build_app(const std::string& document_path, const AppBuildOptions& options, Diagnostics& diagnostics, std::ostream* log = nullptr);

struct LoadedApp {
    Project project;
    ScriptBuild scripts;
    std::string window_id;
};

bool load_app(const Bundle& bundle, LoadedApp& app, std::string& error);

} // namespace grove

#endif // GROVE_APP_HPP
