#include "grove/pages.hpp"

#include "grove/markup/events.hpp"

#include <filesystem>

namespace grove {

namespace {

const Node* tree_root(const Node& any) {
    const Node* n = &any;
    while (n->parent()) n = n->parent();
    return n;
}

const Node* find_id(const Node& node, std::string_view id) {
    if (node.is_element() && node.id() == id) return &node;
    for (const auto& c : node.children()) {
        if (const Node* found = find_id(*c, id)) return found;
    }
    return nullptr;
}

std::string_view trimmed_target(std::string_view target) {
    while (!target.empty() && (target.front() == ' ' || target.front() == '#')) target.remove_prefix(1);
    while (!target.empty() && target.back() == ' ') target.remove_suffix(1);
    return target;
}

std::string normalized_src(std::string_view src) {
    return std::filesystem::path(std::string(src)).lexically_normal().generic_string();
}

const Node* find_page_by_src(const Node& node, const std::string& src) {
    if (node.is_element() && node.tag() == "page" && node.has_attribute("src") && normalized_src(node.attribute_or("src", "")) == src) return &node;
    for (const auto& c : node.children()) {
        if (const Node* found = find_page_by_src(*c, src)) return found;
    }
    return nullptr;
}

void fire(Document& document, Node& target, const char* type) {
    Event e;
    e.type = type;
    document.events().dispatch(document, &target, e);
}

bool activate(Document& document, Node& page, bool remember) {
    if (!is_page(page)) return false;
    for (Node* up = page.parent(); up; up = up->parent()) {
        if (is_page(*up) && !is_page_current(*up)) activate(document, *up, false);
    }
    Node* parent = page.parent();
    Node* before = parent ? current_page(*parent) : nullptr;
    if (before == &page && page.has_attribute("current")) return true;
    if (parent) {
        for (const auto& c : parent->children()) {
            if (c.get() != &page && is_page(*c)) c->remove_attribute("current");
        }
    }
    page.set_attribute("current", "");
    if (remember && before && before != &page && !before->id().empty()) document.page_history().push_back(before->id());
    document.touch();
    if (before && before != &page) fire(document, *before, "page_hide");
    if (before != &page) fire(document, page, "page_show");
    return true;
}

} // namespace

bool is_page(const Node& node) noexcept {
    return node.is_element() && node.tag() == "page";
}

bool is_modal(const Node& node) noexcept {
    return node.is_element() && node.tag() == "modal";
}

bool is_page_current(const Node& page) {
    if (!is_page(page)) return false;
    if (page.has_attribute("current")) return true;
    const Node* parent = page.parent();
    if (!parent) return true;
    const Node* first = nullptr;
    for (const auto& c : parent->children()) {
        if (!is_page(*c)) continue;
        if (c->has_attribute("current")) return false;
        if (!first) first = c.get();
    }
    return first == &page;
}

bool is_page_shown(const Node& node) {
    for (const Node* n = &node; n; n = n->parent()) {
        if (is_page(*n) && !is_page_current(*n)) return false;
    }
    return true;
}

const Node* find_in_tree(const Node& any, std::string_view id) {
    if (id.empty()) return nullptr;
    return find_id(*tree_root(any), id);
}

bool is_navigation_current(const Node& node) {
    if (is_page(node)) return is_page_current(node);
    const Attribute* to = node.find_attribute("to");
    if (!to) return false;
    const std::string_view target = trimmed_target(to->value);
    const Node* page = nullptr;
    if (target.size() > 6 && target.substr(target.size() - 6) == ".grove") page = find_page_by_src(*tree_root(node), normalized_src(target));
    else page = find_in_tree(node, target);
    if (!page) return false;
    if (is_page(*page)) return is_page_shown(*page);
    if (is_modal(*page)) return page->has_attribute("open");
    return false;
}

Node* find_page(Document& document, std::string_view target) {
    target = trimmed_target(target);
    if (target.empty()) return nullptr;
    if (target.size() > 6 && target.substr(target.size() - 6) == ".grove") return const_cast<Node*>(find_page_by_src(document.root(), normalized_src(target)));
    return document.find_by_id(target);
}

Node* current_page(Node& group) {
    for (const auto& c : group.children()) {
        if (is_page(*c) && is_page_current(*c)) return c.get();
    }
    return nullptr;
}

bool show_page(Document& document, Node& page) {
    return activate(document, page, true);
}

bool go_back(Document& document) {
    std::vector<std::string>& history = document.page_history();
    while (!history.empty()) {
        const std::string id = history.back();
        history.pop_back();
        Node* page = document.find_by_id(id);
        if (page && is_page(*page) && !is_page_shown(*page)) return activate(document, *page, false);
    }
    return false;
}

bool open_modal(Document& document, Node& modal) {
    if (!is_modal(modal)) return false;
    if (modal.has_attribute("open")) return true;
    modal.set_attribute("open", "");
    document.touch();
    fire(document, modal, "open");
    return true;
}

bool close_modal(Document& document, Node& modal) {
    if (!is_modal(modal) || !modal.has_attribute("open")) return false;
    modal.remove_attribute("open");
    document.touch();
    fire(document, modal, "close");
    return true;
}

bool follow_link(Document& document, Node* clicked) {
    for (Node* n = clicked; n; n = n->parent()) {
        if (!n->is_element() || n->tag() == "window" || n->tag() == "body") break;
        if (n->has_attribute("disabled")) return false;
        if (n->has_attribute("to")) return navigate(document, n->attribute_or("to", ""), n);
    }
    return false;
}

std::vector<Node*> open_modals(Node& scope) {
    std::vector<Node*> out;
    scope.walk([&out](Node& n) {
        if (is_modal(n) && n.has_attribute("open")) out.push_back(&n);
    });
    return out;
}

bool navigate(Document& document, std::string_view raw, Node* from) {
    const std::string_view target = trimmed_target(raw);
    if (target.empty()) return false;
    if (target == "back") return go_back(document);
    if (target == "close") {
        for (Node* n = from; n; n = n->parent()) {
            if (is_modal(*n)) return close_modal(document, *n);
        }
        std::vector<Node*> open = open_modals(document.root());
        return !open.empty() && close_modal(document, *open.back());
    }
    Node* node = find_page(document, target);
    if (!node) return false;
    if (is_page(*node)) {
        const bool shown = show_page(document, *node);
        for (Node* n = from; shown && n; n = n->parent()) {
            if (is_modal(*n) && !node->is_inside(n)) {
                close_modal(document, *n);
                break;
            }
        }
        return shown;
    }
    if (is_modal(*node)) return open_modal(document, *node);
    return false;
}

} // namespace grove
