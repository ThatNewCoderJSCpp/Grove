#include "grove/project.hpp"
#include "grove/markup/parser.hpp"
#include "grove/bark/parser.hpp"
#include "grove/input/input.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace grove {

namespace {

void load_bark_file(const std::string& path, Project& project, std::set<std::string>& seen, const Span& from) {
    const std::string key = std::filesystem::weakly_canonical(path).string();
    if (!seen.insert(key).second) return;
    const std::optional<std::string> text = read_file(path);
    if (!text) {
        project.diagnostics.error(project.path, from, "can't read stylesheet '" + path + "'");
        return;
    }
    bark::Stylesheet sheet = bark::parse_stylesheet(*text, path, project.diagnostics, Position{}, project.styles.sheet().variables());
    const std::string dir = std::filesystem::path(path).parent_path().string();
    for (const std::string& import : sheet.imports()) load_bark_file(resolve_path(dir, import), project, seen, from);
    project.styles.add(sheet);
}

void load_styles(Project& project) {
    std::set<std::string> seen;
    for (const Node* el : project.document.elements_by_tag("style")) {
        if (const std::optional<std::string> src = el->attribute("src")) {
            load_bark_file(resolve_path(project.base_dir, *src), project, seen, el->span());
            continue;
        }
        const std::string source = el->inner_text();
        const Position origin = el->children().empty() ? el->span().end : el->children().front()->text_span().begin;
        bark::Stylesheet sheet = bark::parse_stylesheet(source, project.path, project.diagnostics, origin, project.styles.sheet().variables());
        project.document.marks().insert(project.document.marks().end(), sheet.marks().begin(), sheet.marks().end());
        for (const std::string& import : sheet.imports()) load_bark_file(resolve_path(project.base_dir, import), project, seen, el->span());
        project.styles.add(sheet);
    }
}

void check_style_attributes(Project& project) {
    project.document.root().walk([&project](const Node& node) {
        const Attribute* a = node.find_attribute("style");
        if (!a) return;
        bark::parse_declarations(a->value, project.path, project.diagnostics, project.styles.sheet().variables(), a->value_span.begin, project.document.marks());
    });
}

void check_structure(Project& project) {
    const Node& root = project.document.root();
    std::size_t bodies = 0;
    for (const auto& c : root.children()) {
        if (c->is_text()) {
            project.diagnostics.warning(project.path, c->span(), "text outside of any element is ignored");
        } else if (c->is_element() && c->tag() == "body") {
            ++bodies;
        }
    }
    if (bodies > 1) project.diagnostics.error(project.path, root.span(), "a document can only have one <body>");
    if (project.document.elements_by_tag("body").empty() && project.document.elements_by_tag("window").empty()) {
        project.diagnostics.note(project.path, Span{}, "document has no <body>; the whole document is drawn in the window");
    }
}

FileReadObserver& file_read_observer() {
    static FileReadObserver observer;
    return observer;
}

std::map<std::string, std::string>& virtual_files() {
    static std::map<std::string, std::string> files;
    return files;
}

} // namespace

std::string virtual_file_key(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}

bool path_exists(const std::string& path) {
    if (!virtual_files().empty() && virtual_files().count(virtual_file_key(path))) return true;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void set_file_read_observer(FileReadObserver observer) {
    file_read_observer() = std::move(observer);
}

void set_virtual_files(std::map<std::string, std::string> files) {
    std::map<std::string, std::string> keyed;
    for (auto& [path, text] : files) keyed[virtual_file_key(path)] = std::move(text);
    virtual_files() = std::move(keyed);
}

std::optional<std::string> read_file(const std::string& path) {
    if (!virtual_files().empty()) {
        const auto it = virtual_files().find(virtual_file_key(path));
        if (it != virtual_files().end()) return it->second;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (file_read_observer()) file_read_observer()(path, text);
    return text;
}

std::string resolve_path(const std::string& base_dir, const std::string& relative) {
    const std::filesystem::path p(relative);
    if (p.is_absolute() || base_dir.empty()) return p.lexically_normal().string();
    return (std::filesystem::path(base_dir) / p).lexically_normal().string();
}

Project load_project_from_source(std::string_view source, const std::string& path, const std::string& base_dir) {
    Project project;
    project.path = path;
    project.base_dir = base_dir;
    project.document = parse_markup(source, path, project.diagnostics);
    check_structure(project);
    project.styles.add(bark::default_stylesheet());
    load_styles(project);
    check_style_attributes(project);
    project.scripts = collect_scripts(project.document, base_dir, project.diagnostics);
    project.math = collect_math(project.document, project.diagnostics);
    check_inputs(project.document, project.diagnostics);
    return project;
}

Project load_project(const std::string& path) {
    const std::optional<std::string> text = read_file(path);
    const std::string base_dir = std::filesystem::path(path).parent_path().string();
    if (!text) {
        Project project;
        project.path = path;
        project.base_dir = base_dir;
        project.diagnostics.error(path, Span{}, "can't read '" + path + "'");
        return project;
    }
    return load_project_from_source(*text, path, base_dir);
}

} // namespace grove
