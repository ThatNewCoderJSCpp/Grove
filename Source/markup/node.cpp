#include "grove/markup/node.hpp"

#include <algorithm>
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

Node* Node::insert_child(std::size_t index, std::unique_ptr<Node> child) {
    child->m_parent = this;
    index = std::min(index, m_children.size());
    auto it = m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
    return it->get();
}

std::unique_ptr<Node> Node::remove_child(const Node* child) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (it->get() != child) continue;
        std::unique_ptr<Node> out = std::move(*it);
        m_children.erase(it);
        out->m_parent = nullptr;
        return out;
    }
    return nullptr;
}

std::size_t Node::index_in_parent() const noexcept {
    if (!m_parent) return 0;
    const auto& siblings = m_parent->m_children;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == this) return i;
    }
    return 0;
}

bool Node::is_inside(const Node* ancestor) const noexcept {
    for (const Node* n = this; n; n = n->m_parent) {
        if (n == ancestor) return true;
    }
    return false;
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

Node* Document::create_element(std::string tag) {
    m_detached.push_back(Node::make_element(std::move(tag), Span{}));
    touch();
    return m_detached.back().get();
}

Node* Document::create_text(std::string text) {
    m_detached.push_back(Node::make_text(std::move(text), Span{}));
    return m_detached.back().get();
}

bool Document::is_attached(const Node& node) const noexcept {
    return node.is_inside(m_root.get());
}

std::unique_ptr<Node> Document::take_detached(Node& node) {
    if (Node* parent = node.parent()) return parent->remove_child(&node);
    for (auto it = m_detached.begin(); it != m_detached.end(); ++it) {
        if (it->get() != &node) continue;
        std::unique_ptr<Node> out = std::move(*it);
        m_detached.erase(it);
        return out;
    }
    return nullptr;
}

Node* Document::append_child(Node& parent, Node& child) {
    return insert_before(parent, child, nullptr);
}

Node* Document::insert_before(Node& parent, Node& child, const Node* reference) {
    if (!parent.is_element() || &child == &parent || parent.is_inside(&child)) return nullptr;
    if (reference && reference->parent() != &parent) return nullptr;
    std::unique_ptr<Node> owned = take_detached(child);
    if (!owned) return nullptr;
    const std::size_t index = reference ? reference->index_in_parent() : parent.children().size();
    Node* placed = parent.insert_child(index, std::move(owned));
    touch();
    return placed;
}

bool Document::remove(Node& node) {
    if (&node == m_root.get() || !node.parent()) return false;
    std::unique_ptr<Node> owned = node.parent()->remove_child(&node);
    if (!owned) return false;
    owned->walk([](Node& n) { n.set_state(StateHover | StateActive | StateFocus, false); });
    m_detached.push_back(std::move(owned));
    touch();
    return true;
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
