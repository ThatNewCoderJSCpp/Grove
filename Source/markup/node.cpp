#include "grove/markup/node.hpp"

#include <sstream>

namespace grove {

std::unique_ptr<Node> Node::make_text(std::string text, Span span) {
    auto node = std::make_unique<Node>(NodeKind::Text, "#text", span);
    node->m_text = std::move(text);
    node->m_text_span = span;
    return node;
}

std::unique_ptr<Node> Node::make_raw(std::string text, Span span) {
    auto node = std::make_unique<Node>(NodeKind::Raw, "#raw", span);
    node->m_text = std::move(text);
    node->m_text_span = span;
    return node;
}

const Attribute* Node::find_attribute(std::string_view name) const noexcept {
    for (const Attribute& a : m_attributes) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

std::optional<std::string> Node::attribute(std::string_view name) const {
    const Attribute* a = find_attribute(name);
    if (!a) return std::nullopt;
    return a->value;
}

std::string Node::attribute_or(std::string_view name, std::string fallback) const {
    const Attribute* a = find_attribute(name);
    return a ? a->value : std::move(fallback);
}

void Node::set_attribute(std::string_view name, std::string value) {
    for (Attribute& a : m_attributes) {
        if (a.name == name) {
            a.value = std::move(value);
            a.has_value = true;
            return;
        }
    }
    Attribute a;
    a.name = std::string(name);
    a.value = std::move(value);
    a.has_value = true;
    m_attributes.push_back(std::move(a));
}

bool Node::remove_attribute(std::string_view name) {
    for (auto it = m_attributes.begin(); it != m_attributes.end(); ++it) {
        if (it->name == name) {
            m_attributes.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Node::classes() const {
    std::vector<std::string> out;
    const Attribute* a = find_attribute("class");
    if (!a) return out;
    std::istringstream in(a->value);
    std::string name;
    while (in >> name) out.push_back(name);
    return out;
}

bool Node::has_class(std::string_view name) const {
    for (const std::string& c : classes()) {
        if (c == name) return true;
    }
    return false;
}

Node* Node::append_child(std::unique_ptr<Node> child) {
    child->m_parent = this;
    m_children.push_back(std::move(child));
    return m_children.back().get();
}

std::string Node::inner_text() const {
    if (!is_element()) return m_text;
    std::string out;
    for (const auto& c : m_children) out += c->inner_text();
    return out;
}

void Node::set_inner_text(std::string text) {
    if (!is_element()) {
        m_text = std::move(text);
        return;
    }
    const bool raw = !m_children.empty() && m_children.front()->is_raw();
    m_children.clear();
    append_child(raw ? make_raw(std::move(text), m_span) : make_text(std::move(text), m_span));
}

void Node::walk(const std::function<void(Node&)>& fn) {
    fn(*this);
    for (auto& c : m_children) c->walk(fn);
}

void Node::walk(const std::function<void(const Node&)>& fn) const {
    fn(*this);
    for (const auto& c : m_children) static_cast<const Node&>(*c).walk(fn);
}

Node* Document::find_by_id(std::string_view id) {
    return const_cast<Node*>(static_cast<const Document&>(*this).find_by_id(id));
}

const Node* Document::find_by_id(std::string_view id) const {
    const Node* found = nullptr;
    m_root->walk([&](const Node& n) {
        if (found || !n.is_element()) return;
        const Attribute* a = n.find_attribute("id");
        if (a && a->value == id) found = &n;
    });
    return found;
}

std::vector<Node*> Document::elements_by_tag(std::string_view tag) {
    std::vector<Node*> out;
    m_root->walk([&](Node& n) {
        if (n.is_element() && n.tag() == tag) out.push_back(&n);
    });
    return out;
}

std::vector<const Node*> Document::elements_by_tag(std::string_view tag) const {
    std::vector<const Node*> out;
    m_root->walk([&](const Node& n) {
        if (n.is_element() && n.tag() == tag) out.push_back(&n);
    });
    return out;
}

Node* Document::first_by_tag(std::string_view tag) {
    return const_cast<Node*>(static_cast<const Document&>(*this).first_by_tag(tag));
}

const Node* Document::first_by_tag(std::string_view tag) const {
    const Node* found = nullptr;
    m_root->walk([&](const Node& n) {
        if (!found && n.is_element() && n.tag() == tag) found = &n;
    });
    return found;
}

namespace {

std::string escape_for_dump(const std::string& text, std::size_t limit) {
    std::string out;
    for (char c : text) {
        if (out.size() >= limit) {
            out += "...";
            break;
        }
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

} // namespace

void dump(const Node& node, std::ostream& os, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    if (node.is_text()) {
        os << indent << "text \"" << escape_for_dump(node.text(), 60) << "\"\n";
        return;
    }
    if (node.is_raw()) {
        os << indent << "raw \"" << escape_for_dump(node.text(), 60) << "\"\n";
        return;
    }
    os << indent << '<' << node.tag();
    for (const Attribute& a : node.attributes()) {
        os << ' ' << a.name;
        if (a.has_value) os << "=\"" << escape_for_dump(a.value, 40) << '"';
    }
    os << ">  @" << node.span().begin.line << ':' << node.span().begin.column << '\n';
    for (const auto& c : node.children()) dump(*c, os, depth + 1);
}

} // namespace grove
