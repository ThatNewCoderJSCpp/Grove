#ifndef GROVE_MARKUP_NODE_HPP
#define GROVE_MARKUP_NODE_HPP

#include "../common/diagnostics.hpp"

#include <cstdint>
#include <functional>
#include <ostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

enum class NodeKind {
    Element,
    Text,
    Raw
};

struct Attribute {
    std::string name;
    std::string value;
    bool has_value = false;
    Span span;
};

class Node {
public:
    Node(NodeKind kind, std::string name, Span span) : m_kind(kind), m_name(std::move(name)), m_span(span) {}

    NodeKind kind() const noexcept { return m_kind; }
    bool is_element() const noexcept { return m_kind == NodeKind::Element; }
    bool is_text() const noexcept { return m_kind == NodeKind::Text; }
    bool is_raw() const noexcept { return m_kind == NodeKind::Raw; }

    const std::string& tag() const noexcept { return m_name; }
    const Span& span() const noexcept { return m_span; }
    void set_span(const Span& span) noexcept { m_span = span; }
    Node* parent() const noexcept { return m_parent; }

    const std::string& text() const noexcept { return m_text; }
    void set_text(std::string text) { m_text = std::move(text); }
    const Span& text_span() const noexcept { return m_text_span; }
    void set_text_span(const Span& span) noexcept { m_text_span = span; }

    const std::vector<Attribute>& attributes() const noexcept { return m_attributes; }
    bool has_attribute(std::string_view name) const noexcept { return find_attribute(name) != nullptr; }
    const Attribute* find_attribute(std::string_view name) const noexcept;
    std::optional<std::string> attribute(std::string_view name) const;
    std::string attribute_or(std::string_view name, std::string fallback) const;
    void set_attribute(std::string_view name, std::string value);
    bool remove_attribute(std::string_view name);
    void add_attribute(Attribute attribute) { m_attributes.push_back(std::move(attribute)); }

    std::string id() const { return attribute_or("id", ""); }
    std::vector<std::string> classes() const;
    bool has_class(std::string_view name) const;

    const std::vector<std::unique_ptr<Node>>& children() const noexcept { return m_children; }
    Node* append_child(std::unique_ptr<Node> child);
    void clear_children() noexcept { m_children.clear(); }

    std::string inner_text() const;
    void set_inner_text(std::string text);

    void walk(const std::function<void(Node&)>& fn);
    void walk(const std::function<void(const Node&)>& fn) const;

    static std::unique_ptr<Node> make_element(std::string tag, Span span) { return std::make_unique<Node>(NodeKind::Element, std::move(tag), span); }
    static std::unique_ptr<Node> make_text(std::string text, Span span);
    static std::unique_ptr<Node> make_raw(std::string text, Span span);

private:
    NodeKind m_kind;
    std::string m_name;
    Span m_span;
    Span m_text_span;
    Node* m_parent = nullptr;
    std::string m_text;
    std::vector<Attribute> m_attributes;
    std::vector<std::unique_ptr<Node>> m_children;
};

class Document {
public:
    Document() : m_root(Node::make_element("#document", Span{})) {}

    Node& root() noexcept { return *m_root; }
    const Node& root() const noexcept { return *m_root; }

    const std::string& path() const noexcept { return m_path; }
    void set_path(std::string path) { m_path = std::move(path); }

    Node* find_by_id(std::string_view id);
    const Node* find_by_id(std::string_view id) const;
    std::vector<Node*> elements_by_tag(std::string_view tag);
    std::vector<const Node*> elements_by_tag(std::string_view tag) const;
    Node* first_by_tag(std::string_view tag);
    const Node* first_by_tag(std::string_view tag) const;

    std::uint64_t revision() const noexcept { return m_revision; }
    void touch() noexcept { ++m_revision; }

private:
    std::unique_ptr<Node> m_root;
    std::string m_path;
    std::uint64_t m_revision = 0;
};

void dump(const Node& node, std::ostream& os, int depth = 0);

} // namespace grove

#endif // GROVE_MARKUP_NODE_HPP
