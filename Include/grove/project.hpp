#ifndef GROVE_PROJECT_HPP
#define GROVE_PROJECT_HPP

#include "markup/node.hpp"
#include "bark/cascade.hpp"
#include "script/script.hpp"
#include "math/math_block.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

enum class Target {
    Desktop = 0,
    Android
};

const char* target_name(Target target) noexcept;
std::optional<Target> parse_target(std::string_view name) noexcept;

struct Project {
    std::string path;
    std::string base_dir;
    Document document;
    bark::StyleEngine styles;
    std::vector<ScriptBlock> scripts;
    std::vector<MathBlock> math;
    Diagnostics diagnostics;
    Target target = Target::Desktop;

    bool android() const noexcept { return target == Target::Android; }
};

using FileReadObserver = std::function<void(const std::string& path, const std::string& text)>;

std::optional<std::string> read_file(const std::string& path);
void set_file_read_observer(FileReadObserver observer);
void set_virtual_files(std::map<std::string, std::string> files);
std::string virtual_file_key(const std::string& path);
bool path_exists(const std::string& path);
std::string resolve_path(const std::string& base_dir, const std::string& relative);

Project load_project(const std::string& path);

std::vector<std::string> split_asset_list(const std::string& text);
std::vector<std::string> icon_paths(const Project& project, const Node& window);
std::string image_path(const Project& project, const Node& img);
Project load_project_from_source(std::string_view source, const std::string& path, const std::string& base_dir);

} // namespace grove

#endif // GROVE_PROJECT_HPP
