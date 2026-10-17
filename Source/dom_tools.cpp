#include "grove/dom_tools.hpp"
#include "grove/bark/parser.hpp"

#include <algorithm>
#include <sstream>

namespace grove {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

std::optional<std::vector<bark::Selector>> selectors_of(std::string_view selector, Diagnostics* diagnostics) {
    Diagnostics local;
    auto parsed = bark::parse_selectors(selector, diagnostics ? *diagnostics : local);
    return parsed;
}

bool matches_any(const std::vector<bark::Selector>& selectors, const Node& node) {
    for (const bark::Selector& s : selectors) {
        if (s.matches(node)) return true;
    }
    return false;
}

void set_classes(Document& document, Node& node, const std::vector<std::string>& classes) {
    std::string joined;
    for (const std::string& c : classes) {
        if (!joined.empty()) joined += ' ';
        joined += c;
    }
    if (joined.empty()) node.remove_attribute("class");
    else node.set_attribute("class", joined);
    document.touch();
}

} // namespace

Node* query(Node& scope, std::string_view selector, Diagnostics* diagnostics) {
    const auto selectors = selectors_of(selector, diagnostics);
    if (!selectors) return nullptr;
    Node* found = nullptr;
    scope.walk([&](Node& n) {
        if (!found && &n != &scope && matches_any(*selectors, n)) found = &n;
    });
    return found;
}

std::vector<Node*> query_all(Node& scope, std::string_view selector, Diagnostics* diagnostics) {
    std::vector<Node*> out;
    const auto selectors = selectors_of(selector, diagnostics);
    if (!selectors) return out;
    scope.walk([&](Node& n) {
        if (&n != &scope && matches_any(*selectors, n)) out.push_back(&n);
    });
    return out;
}

bool add_class(Document& document, Node& node, std::string_view name) {
    std::vector<std::string> classes = node.classes();
    if (name.empty() || std::find(classes.begin(), classes.end(), name) != classes.end()) return false;
    classes.emplace_back(name);
    set_classes(document, node, classes);
    return true;
}

bool remove_class(Document& document, Node& node, std::string_view name) {
    std::vector<std::string> classes = node.classes();
    const auto it = std::find(classes.begin(), classes.end(), name);
    if (it == classes.end()) return false;
    classes.erase(it);
    set_classes(document, node, classes);
    return true;
}

bool toggle_class(Document& document, Node& node, std::string_view name) {
    if (remove_class(document, node, name)) return false;
    add_class(document, node, name);
    return true;
}

std::vector<std::pair<std::string, std::string>> split_style_attribute(std::string_view style) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string current;
    int depth = 0;
    char quote = 0;
    auto flush = [&]() {
        const std::size_t colon = current.find(':');
        if (colon != std::string::npos) {
            std::string property = trim(std::string_view(current).substr(0, colon));
            std::string value = trim(std::string_view(current).substr(colon + 1));
            if (!property.empty()) out.emplace_back(std::move(property), std::move(value));
        }
        current.clear();
    };
    for (char c : style) {
        if (quote) {
            if (c == quote) quote = 0;
            current += c;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (c == ';' && depth == 0) {
            flush();
            continue;
        }
        current += c;
    }
    flush();
    return out;
}

std::optional<std::string> inline_style(const Node& node, std::string_view property) {
    const Attribute* a = node.find_attribute("style");
    if (!a) return std::nullopt;
    std::optional<std::string> found;
    for (const auto& [p, v] : split_style_attribute(a->value)) {
        if (p == property) found = v;
    }
    return found;
}

void set_inline_style(Document& document, Node& node, std::string_view property, std::string_view value) {
    std::vector<std::pair<std::string, std::string>> decls;
    if (const Attribute* a = node.find_attribute("style")) decls = split_style_attribute(a->value);
    const std::string v = trim(value);
    bool replaced = false;
    for (auto it = decls.begin(); it != decls.end();) {
        if (it->first != property) {
            ++it;
            continue;
        }
        if (v.empty() || replaced) {
            it = decls.erase(it);
            continue;
        }
        it->second = v;
        replaced = true;
        ++it;
    }
    if (!replaced && !v.empty()) decls.emplace_back(std::string(property), v);
    std::string joined;
    for (const auto& [p, val] : decls) {
        if (!joined.empty()) joined += ' ';
        joined += p + ": " + val + ";";
    }
    if (joined.empty()) node.remove_attribute("style");
    else node.set_attribute("style", joined);
    document.touch();
}

} // namespace grove
