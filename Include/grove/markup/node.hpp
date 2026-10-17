#ifndef GROVE_MARKUP_NODE_HPP
#define GROVE_MARKUP_NODE_HPP

#include "../common/diagnostics.hpp"
#include "../common/marks.hpp"
#include "events.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <ostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

enum NodeState : unsigned {
    StateHover = 1u,
    StateActive = 2u,
    StateFocus = 4u
};

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
    Span name_span;
    Span value_span;
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
    Node* insert_child(std::size_t index, std::unique_ptr<Node> child);
    std::unique_ptr<Node> remove_child(const Node* child);
    std::size_t index_in_parent() const noexcept;
    bool is_inside(const Node* ancestor) const noexcept;
    void clear_children() noexcept { m_children.clear(); }

    unsigned state() const noexcept { return m_state; }
    bool has_state(unsigned bits) const noexcept { return (m_state & bits) == bits; }
    void set_state(unsigned bits, bool on) noexcept { m_state = on ? (m_state | bits) : (m_state & ~bits); }

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
    unsigned m_state = 0;
};

using NativeFunction = std::function<std::string(const std::vector<std::string>& args)>;

struct FileDialogRequest {
    std::string mode = "open";
    std::string title;
    std::string filter;
    std::string start;
    bool multiple = false;
    Node* owner = nullptr;
};

struct DocumentHost {
    std::function<std::string(const Node&, const std::string&)> computed_style;
    std::function<bool(Node*)> focus;
    double viewport_width = 0.0;
    double viewport_height = 0.0;
    Node* main_window = nullptr;
    std::function<bool(const Node&, double&, double&)> window_size;
    std::function<bool(Node&, double, double)> resize_window;
    std::function<bool(Node&, double, double)> move_window;
    std::function<bool(Node&)> focus_window;
    std::map<std::string, NativeFunction, std::less<>> natives;
    std::function<std::vector<std::string>(const FileDialogRequest&)> file_dialog;
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

    DocumentHost& host() noexcept { return *m_host; }
    const DocumentHost& host() const noexcept { return *m_host; }

    EventRegistry& events() noexcept { return *m_events; }
    const EventRegistry& events() const noexcept { return *m_events; }

    Node* create_element(std::string tag);
    Node* create_text(std::string text);
    Node* append_child(Node& parent, Node& child);
    Node* insert_before(Node& parent, Node& child, const Node* reference);
    bool remove(Node& node);
    bool is_attached(const Node& node) const noexcept;

    Marks& marks() noexcept { return m_marks; }
    const Marks& marks() const noexcept { return m_marks; }

    std::uint64_t revision() const noexcept { return m_revision; }
    void touch() noexcept { ++m_revision; }

private:
    std::unique_ptr<Node> m_root;
    std::unique_ptr<Node> take_detached(Node& node);

    std::string m_path;
    Marks m_marks;
    std::vector<std::unique_ptr<Node>> m_detached;
    std::unique_ptr<EventRegistry> m_events = std::make_unique<EventRegistry>();
    std::unique_ptr<DocumentHost> m_host = std::make_unique<DocumentHost>();
    std::uint64_t m_revision = 0;
};

void dump(const Node& node, std::ostream& os, int depth = 0);

} // namespace grove

#endif // GROVE_MARKUP_NODE_HPP
