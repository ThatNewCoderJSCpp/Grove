#ifndef GROVE_SCRIPT_BUILDER_HPP
#define GROVE_SCRIPT_BUILDER_HPP

#include "script.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace grove {

struct Project;

struct ScriptTools {
    std::string walnut;
    std::string cxx;
    std::string cc;
    std::string include_dir;
    std::string cache_dir;
    bool rebuild = false;
    bool verbose = false;
    bool portable = false;
    bool compiler_from_walnut = false;
    bool cross = false;
    std::string target;
    std::vector<std::string> cxx_flags;
    std::vector<std::string> link_flags;
    std::vector<std::string> walnut_args;
};

struct BuiltScript {
    std::size_t index = 0;
    ScriptBlock script;
    std::string module_path;
    bool ok = false;
    bool cached = false;
    bool runnable = true;
};

struct ScriptBuild {
    std::vector<BuiltScript> scripts;
    bool ok() const;
    std::size_t runnable_count() const;
};

std::string executable_path();
std::string module_extension();
ScriptTools default_script_tools(const std::string& document_path);
std::string describe(const ScriptTools& tools);
std::string walnut_compiler(const std::string& walnut);

ScriptBuild build_scripts(Project& project, const ScriptTools& tools, std::ostream* log = nullptr);

} // namespace grove

#endif // GROVE_SCRIPT_BUILDER_HPP
