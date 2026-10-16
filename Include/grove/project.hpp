#ifndef GROVE_PROJECT_HPP
#define GROVE_PROJECT_HPP

#include "markup/node.hpp"
#include "bark/cascade.hpp"
#include "script/script.hpp"
#include "math/math_block.hpp"

#include <optional>
#include <string>
#include <vector>

namespace grove {

struct Project {
    std::string path;
    std::string base_dir;
    Document document;
    bark::StyleEngine styles;
    std::vector<ScriptBlock> scripts;
    std::vector<MathBlock> math;
    Diagnostics diagnostics;
};

std::optional<std::string> read_file(const std::string& path);
std::string resolve_path(const std::string& base_dir, const std::string& relative);

Project load_project(const std::string& path);
Project load_project_from_source(std::string_view source, const std::string& path, const std::string& base_dir);

} // namespace grove

#endif // GROVE_PROJECT_HPP
