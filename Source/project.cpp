#include "grove/project.hpp"
#include "grove/markup/parser.hpp"
#include "grove/bark/parser.hpp"
#include "grove/input/input.hpp"
#include "grove/input/quantity.hpp"

#include <algorithm>
#include <cctype>
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

void rebase_sources(Node& root, const std::string& from_dir, const std::string& base_dir) {
    root.walk([&](Node& n) {
        if (!n.is_element()) return;
        for (const char* attr : { "src", "icon" }) {
            const std::optional<std::string> value = n.attribute(attr);
            if (!value || value->empty()) continue;
            std::vector<std::string> parts = std::string(attr) == "icon" ? split_asset_list(*value) : std::vector<std::string>{ *value };
            std::string joined;
            for (const std::string& part : parts) {
                std::filesystem::path full(resolve_path(from_dir, part));
                std::filesystem::path shown = full;
                if (!base_dir.empty() && !std::filesystem::path(part).is_absolute()) {
                    const std::filesystem::path rel = full.lexically_relative(base_dir);
                    if (!rel.empty()) shown = rel;
                }
                if (!joined.empty()) joined += ' ';
                joined += shown.generic_string();
            }
            n.set_attribute(attr, joined);
        }
    });
}

void include_pages(Project& project, Node& scope, std::vector<std::string>& chain) {
    std::vector<Node*> pages;
    scope.walk([&pages](Node& n) {
        if (n.is_element() && n.tag() == "page" && n.has_attribute("src")) pages.push_back(&n);
    });
    for (Node* page : pages) {
        if (page->has_attribute("included")) continue;
        const std::string src = page->attribute_or("src", "");
        const std::string path = resolve_path(project.base_dir, src);
        const std::string key = std::filesystem::path(path).lexically_normal().generic_string();
        if (std::find(chain.begin(), chain.end(), key) != chain.end()) {
            project.diagnostics.error(project.path, page->span(), "page '" + src + "' includes itself");
            continue;
        }
        const std::optional<std::string> text = read_file(path);
        if (!text) {
            project.diagnostics.error(project.path, page->span(), "can't read page '" + src + "'");
            continue;
        }
        if (!page->children().empty()) {
            bool only_space = true;
            for (const auto& c : page->children()) {
                if (!c->is_text() || c->text().find_first_not_of(" \t\r\n") != std::string::npos) only_space = false;
            }
            if (!only_space) project.diagnostics.warning(project.path, page->span(), "page '" + src + "' has its own content too; the content of '" + src + "' goes after it");
        }
        Document sub = parse_markup(*text, path, project.diagnostics);
        rebase_sources(sub.root(), std::filesystem::path(path).parent_path().string(), project.base_dir);
        Node* from = sub.first_by_tag("body");
        if (!from) from = &sub.root();
        if (Node* head = sub.first_by_tag("head")) {
            if (!page->has_attribute("title")) {
                if (const Node* t = sub.first_by_tag("title")) page->set_attribute("title", t->inner_text());
            }
            std::vector<const Node*> keep;
            for (const auto& c : head->children()) {
                if (c->is_element() && (c->tag() == "style" || c->tag() == "script")) keep.push_back(c.get());
            }
            for (const Node* c : keep) page->append_child(head->remove_child(c));
        }
        if (from != &sub.root()) {
            for (const char* attr : { "class", "style" }) {
                if (from->has_attribute(attr) && !page->has_attribute(attr)) page->set_attribute(attr, from->attribute_or(attr, ""));
            }
        }
        std::vector<const Node*> moving;
        for (const auto& c : from->children()) {
            if (c->is_element() && (c->tag() == "head" || c->tag() == "grove")) continue;
            moving.push_back(c.get());
        }
        if (from == &sub.root()) {
            if (Node* grove = sub.first_by_tag("grove")) {
                moving.clear();
                for (const auto& c : grove->children()) {
                    if (c->is_element() && c->tag() == "head") continue;
                    moving.push_back(c.get());
                }
                from = grove;
            }
        }
        for (const Node* c : moving) page->append_child(from->remove_child(c));
        page->set_attribute("included", "");
        chain.push_back(key);
        include_pages(project, *page, chain);
        chain.pop_back();
        page->remove_attribute("included");
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

std::vector<std::string> split_asset_list(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text + " ") {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    return out;
}

std::vector<std::string> icon_paths(const Project& project, const Node& window) {
    std::string list = window.attribute_or("icon", "");
    if (list.empty()) {
        if (const Node* body = project.document.first_by_tag("body")) list = body->attribute_or("icon", "");
    }
    std::vector<std::string> out;
    for (const std::string& p : split_asset_list(list)) out.push_back(resolve_path(project.base_dir, p));
    return out;
}

std::string image_path(const Project& project, const Node& img) {
    const std::string src = img.attribute_or("src", "");
    return src.empty() ? std::string() : resolve_path(project.base_dir, src);
}

namespace {

bool valid_package(const std::string& p) {
    if (p.empty()) return false;
    bool start = true;
    int parts = 1;
    for (char c : p) {
        if (c == '.') {
            if (start) return false;
            start = true;
            ++parts;
            continue;
        }
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (start && !letter) return false;
        if (!letter && !digit && c != '_') return false;
        start = false;
    }
    return !start && parts >= 2;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

void apply_declaration(Project& project) {
    const Document& doc = project.document;
    const Span span = doc.declaration_span();
    static const char* const known[] = { "type", "package", "label", "version", "version-code", "icon", "orientation", "min-sdk", "target-sdk", "abi", "fullscreen", "keep-screen-on", "storage", "max-fps", "idle-fps", "vsync" };
    for (const auto& entry : doc.declarations()) {
        const std::string& key = entry.first;
        bool listed = false;
        for (const char* k : known) listed = listed || key == k;
        if (!listed) project.diagnostics.warning(project.path, span, "<!GROVE> doesn't know '" + key + "'");
    }
    const std::string type = doc.declaration("type");
    if (!type.empty()) {
        if (const std::optional<Target> t = parse_target(type)) project.target = *t;
        else project.diagnostics.error(project.path, span, "<!GROVE type=\"" + type + "\"> isn't a target; use desktop or android");
    }
    if (!project.android()) {
        for (const char* key : { "package", "version-code", "orientation", "min-sdk", "target-sdk", "abi", "storage" }) {
            if (doc.has_declaration(key)) project.diagnostics.warning(project.path, span, std::string("'") + key + "' only matters with <!GROVE type=android>");
        }
        return;
    }
    const std::string package = doc.declaration("package");
    if (!package.empty() && !valid_package(package)) project.diagnostics.error(project.path, span, "package \"" + package + "\" isn't an Android package name like com.example.app");
    for (const char* key : { "version-code", "min-sdk", "target-sdk" }) {
        const std::string v = doc.declaration(key);
        if (!v.empty() && !all_digits(v)) project.diagnostics.error(project.path, span, std::string("'") + key + "' has to be a whole number");
    }
    const std::string min_sdk = doc.declaration("min-sdk");
    if (all_digits(min_sdk) && std::stoi(min_sdk) < 26) project.diagnostics.error(project.path, span, "min-sdk " + min_sdk + " is too old; Grove apps need Android 8.0 (26) or newer");
    const std::string orientation = doc.declaration("orientation");
    if (!orientation.empty() && orientation != "portrait" && orientation != "landscape" && orientation != "sensor" && orientation != "any" && orientation != "locked") {
        project.diagnostics.error(project.path, span, "orientation \"" + orientation + "\" isn't portrait, landscape, sensor, any or locked");
    }
    for (const std::string& abi : split_asset_list(doc.declaration("abi"))) {
        if (abi != "arm64-v8a" && abi != "armeabi-v7a" && abi != "x86_64" && abi != "x86") project.diagnostics.error(project.path, span, "abi \"" + abi + "\" isn't arm64-v8a, armeabi-v7a, x86_64 or x86");
    }
    const std::string storage = doc.declaration("storage");
    if (!storage.empty() && storage != "app" && storage != "all") project.diagnostics.error(project.path, span, "storage \"" + storage + "\" isn't app or all");
    const std::string icon = doc.declaration("icon");
    if (!icon.empty() && !read_file(resolve_path(project.base_dir, icon))) project.diagnostics.warning(project.path, span, "can't find the app icon '" + icon + "'");
}

void check_android(Project& project) {
    project.document.root().walk([&](const Node& n) {
        if (!n.is_element()) return;
        if (n.tag() == "terminal") project.diagnostics.warning(project.path, n.span(), "<terminal> has no shell to run on Android");
        if (n.tag() == "input_file" && project.document.declaration("storage") != "all") {
            const std::string mode = n.attribute_or("mode", "open");
            if (mode == "folder") project.diagnostics.warning(project.path, n.span(), "<input_file mode=\"folder\"> can only open folders on Android with <!GROVE storage=all>");
            else if (mode == "save") project.diagnostics.warning(project.path, n.span(), "<input_file mode=\"save\"> can only save outside the app's own storage on Android with <!GROVE storage=all>");
        }
    });
}

void check_assets(Project& project) {
    project.document.root().walk([&](const Node& n) {
        if (!n.is_element()) return;
        std::vector<std::string> paths;
        if ((n.tag() == "body" || n.tag() == "window") && n.has_attribute("icon")) {
            for (const std::string& p : split_asset_list(n.attribute_or("icon", ""))) paths.push_back(p);
        }
        if (n.tag() == "img" && n.has_attribute("src")) paths.push_back(n.attribute_or("src", ""));
        for (const std::string& p : paths) {
            if (!read_file(resolve_path(project.base_dir, p))) project.diagnostics.warning(project.path, n.span(), "can't find image '" + p + "'");
        }
    });
}

} // namespace

Project load_project_from_source(std::string_view source, const std::string& path, const std::string& base_dir) {
    Project project;
    project.path = path;
    project.base_dir = base_dir;
    project.document = parse_markup(source, path, project.diagnostics);
    apply_declaration(project);
    std::vector<std::string> chain{ std::filesystem::path(path).lexically_normal().generic_string() };
    include_pages(project, project.document.root(), chain);
    expand_quantity_inputs(project.document);
    check_structure(project);
    project.styles.add(bark::default_stylesheet());
    load_styles(project);
    check_style_attributes(project);
    project.scripts = collect_scripts(project.document, base_dir, project.diagnostics);
    project.math = collect_math(project.document, project.diagnostics);
    check_inputs(project.document, project.diagnostics);
    check_assets(project);
    if (project.android()) check_android(project);
    return project;
}

const char* target_name(Target target) noexcept {
    switch (target) {
        case Target::Android: return "android";
        default:              return "desktop";
    }
}

std::optional<Target> parse_target(std::string_view name) noexcept {
    std::string n(name);
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "desktop" || n == "pc" || n == "computer") return Target::Desktop;
    if (n == "android") return Target::Android;
    return std::nullopt;
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
