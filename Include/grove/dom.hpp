#ifndef GROVE_DOM_HPP
#define GROVE_DOM_HPP

#include "api/grove_api.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifndef GROVE_SCRIPT_ID
#define GROVE_SCRIPT_ID 0
#endif

namespace grove {

class Element;
class ElementList;

namespace detail {

inline std::string str(const char* s) { return s ? std::string(s) : std::string(); }

inline std::string number(double v) {
    std::string out = std::to_string(v);
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    return out;
}

inline void report(const char* what) { grove_log(what); }

inline void listener_trampoline(grove_event* e, void* user);
inline void listener_release(void* user);
inline void simple_trampoline(grove_event* e, void* user);
inline void simple_release(void* user);
inline void timer_trampoline(void* user);
inline void timer_release(void* user);

struct OnLoadEntry {
    int script = 0;
    void (*fn)() = nullptr;
};

inline std::vector<OnLoadEntry>& on_load_list() {
    static std::vector<OnLoadEntry> list;
    return list;
}

struct OnLoadRegistrar {
    OnLoadRegistrar(int script, void (*fn)()) { on_load_list().push_back(OnLoadEntry{ script, fn }); }
};

inline void run_on_load(int script) {
    for (const OnLoadEntry& e : on_load_list()) {
        if (e.script == script) e.fn();
    }
}

} // namespace detail

class Event {
public:
    Event() = default;
    explicit Event(grove_event* event) : m_event(event) {}

    static Event current() { return Event(grove_current_event(grove_current_document())); }

    bool exists() const { return m_event != nullptr; }
    grove_event* handle() const { return m_event; }

    std::string type() const { return detail::str(grove_event_type(m_event)); }
    inline Element target() const;
    inline Element current_target() const;
    double x() const { return grove_event_x(m_event); }
    double y() const { return grove_event_y(m_event); }
    double delta_x() const { return grove_event_delta_x(m_event); }
    double delta_y() const { return grove_event_delta_y(m_event); }
    std::int64_t button() const { return grove_event_button(m_event); }
    std::string key() const { return detail::str(grove_event_key(m_event)); }
    std::string text() const { return detail::str(grove_event_text(m_event)); }
    bool shift() const { return (grove_event_modifiers(m_event) & GROVE_MOD_SHIFT) != 0; }
    bool ctrl() const { return (grove_event_modifiers(m_event) & GROVE_MOD_CONTROL) != 0; }
    bool alt() const { return (grove_event_modifiers(m_event) & GROVE_MOD_ALT) != 0; }
    bool meta() const { return (grove_event_modifiers(m_event) & GROVE_MOD_META) != 0; }
    void stop_propagation() { grove_event_stop_propagation(m_event); }
    void prevent_default() { grove_event_prevent_default(m_event); }
    bool default_prevented() const { return grove_event_default_prevented(m_event); }

private:
    grove_event* m_event = nullptr;
};

class Element {
public:
    Element() = default;
    explicit Element(grove_element* element) : m_element(element) {}

    bool exists() const { return m_element != nullptr; }
    grove_element* handle() const { return m_element; }
    bool operator==(const Element& other) const { return m_element == other.m_element; }
    bool operator!=(const Element& other) const { return m_element != other.m_element; }

    std::string tag() const { return detail::str(grove_tag(m_element)); }
    std::string id() const { return detail::str(grove_id(m_element)); }

    bool has_attribute(const std::string& name) const { return grove_has_attribute(m_element, name.c_str()) != 0; }
    std::string get_attribute(const std::string& name) const { return detail::str(grove_get_attribute(m_element, name.c_str())); }
    void set_attribute(const std::string& name, const std::string& value) { grove_set_attribute(doc(), m_element, name.c_str(), value.c_str()); }
    void remove_attribute(const std::string& name) { grove_remove_attribute(doc(), m_element, name.c_str()); }

    std::string text() const { return detail::str(grove_text(m_element)); }
    void set_text(const std::string& value) { grove_set_text(doc(), m_element, value.c_str()); }
    std::string inner_text() const { return text(); }
    void set_inner_text(const std::string& value) { set_text(value); }

    bool has_class(const std::string& name) const { return grove_has_class(m_element, name.c_str()); }
    void add_class(const std::string& name) { grove_add_class(doc(), m_element, name.c_str()); }
    void remove_class(const std::string& name) { grove_remove_class(doc(), m_element, name.c_str()); }
    bool toggle_class(const std::string& name) { return grove_toggle_class(doc(), m_element, name.c_str()); }

    std::string style(const std::string& property) const { return detail::str(grove_get_style(m_element, property.c_str())); }
    void set_style(const std::string& property, const std::string& value) { grove_set_style(doc(), m_element, property.c_str(), value.c_str()); }
    void remove_style(const std::string& property) { grove_set_style(doc(), m_element, property.c_str(), ""); }
    std::string computed_style(const std::string& property) const { return detail::str(grove_computed_style(doc(), m_element, property.c_str())); }

    Element parent() const { return Element(grove_parent(m_element)); }
    std::int64_t child_count() const { return static_cast<std::int64_t>(grove_child_count(m_element)); }
    Element child(std::int64_t index) const { return index < 0 ? Element() : Element(grove_child(m_element, static_cast<size_t>(index))); }
    inline ElementList children() const;
    std::int64_t index() const { return static_cast<std::int64_t>(grove_index_in_parent(m_element)); }
    Element next_sibling() const { return parent().child(index() + 1); }
    Element previous_sibling() const { return index() > 0 ? parent().child(index() - 1) : Element(); }

    bool append_child(const Element& child) { return grove_append_child(doc(), m_element, child.m_element); }
    bool insert_before(const Element& child, const Element& reference) { return grove_insert_before(doc(), m_element, child.m_element, reference.m_element); }
    bool remove() { return grove_remove_element(doc(), m_element); }
    bool is_attached() const { return grove_is_attached(doc(), m_element); }

    Element query(const std::string& selector) const { return Element(grove_query(doc(), m_element, selector.c_str())); }
    inline ElementList query_all(const std::string& selector) const;

    std::int64_t add_event_listener(const std::string& type, std::function<void(Event&)> fn) {
        auto* boxed = new std::function<void(Event&)>(std::move(fn));
        return static_cast<std::int64_t>(grove_add_event_listener(doc(), m_element, type.c_str(), &detail::listener_trampoline, boxed, &detail::listener_release));
    }
    std::int64_t on(const std::string& type, std::function<void(Event&)> fn) { return add_event_listener(type, std::move(fn)); }
    std::int64_t listen(const std::string& type, std::function<void()> fn) {
        auto* boxed = new std::function<void()>(std::move(fn));
        return static_cast<std::int64_t>(grove_add_event_listener(doc(), m_element, type.c_str(), &detail::simple_trampoline, boxed, &detail::simple_release));
    }
    bool remove_event_listener(std::int64_t id) { return grove_remove_event_listener(doc(), static_cast<uint64_t>(id)); }
    bool dispatch(const std::string& type) { return grove_dispatch_event(doc(), m_element, type.c_str()); }

    bool focus() { return grove_focus(doc(), m_element); }
    bool hovered() const { return grove_has_state(m_element, "hover"); }
    bool focused() const { return grove_has_state(m_element, "focus"); }
    bool active() const { return grove_has_state(m_element, "active"); }

    std::int64_t input_kind() const { return grove_input_kind_of(m_element); }
    std::int64_t value_integer() const {
        int64_t v = 0;
        grove_input_integer(m_element, &v);
        return static_cast<std::int64_t>(v);
    }
    double value_float() const {
        double v = 0.0;
        grove_input_float(m_element, &v);
        return v;
    }
    bool value_bool() const {
        bool v = false;
        grove_input_bool(m_element, &v);
        return v;
    }
    std::string value_string() const { return detail::str(grove_input_string(m_element)); }
    std::string value_text() const { return detail::str(grove_input_text(m_element)); }
    std::string value_color() const {
        uint32_t v = 0;
        if (!grove_input_color(m_element, &v)) return std::string();
        static const char* digits = "0123456789abcdef";
        std::string out = "#";
        for (int shift = 24; shift >= 0; shift -= 8) {
            if (shift == 0 && (v & 0xFFu) == 0xFFu) break;
            const unsigned byte = (v >> shift) & 0xFFu;
            out += digits[byte >> 4];
            out += digits[byte & 15];
        }
        return out;
    }
    bool set_value_integer(std::int64_t value) { return grove_set_input_integer(doc(), m_element, static_cast<int64_t>(value)); }
    bool set_value_float(double value) { return grove_set_input_float(doc(), m_element, value); }
    bool set_value_bool(bool value) { return grove_set_input_bool(doc(), m_element, value); }
    bool set_value_string(const std::string& value) { return grove_set_input_string(doc(), m_element, value.c_str()); }
    bool set_value_color(const std::string& hex) {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h.erase(0, 1);
        if (h.size() == 6) h += "ff";
        if (h.size() != 8) return false;
        uint32_t v = 0;
        for (char c : h) {
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = (v << 4) | static_cast<uint32_t>(d);
        }
        return grove_set_input_color(doc(), m_element, v);
    }

    Element window() const { return Element(grove_window_of(doc(), m_element)); }
    bool open() { return grove_window_open(doc(), m_element); }
    bool close() { return grove_window_close(doc(), m_element); }
    bool is_open() const { return grove_window_is_open(doc(), m_element); }
    double window_width() const { return grove_window_width(doc(), m_element); }
    double window_height() const { return grove_window_height(doc(), m_element); }
    bool resize_window(double width, double height) { return grove_window_resize(doc(), m_element, width, height); }
    bool move_window(double x, double y) { return grove_window_move(doc(), m_element, x, y); }
    bool focus_window() { return grove_window_focus(doc(), m_element); }
    bool answer(bool ok) { return grove_popup_answer(doc(), m_element, ok); }
    std::string answer_value() const { return get_attribute("value"); }

private:
    static grove_document* doc() { return grove_current_document(); }
    grove_element* m_element = nullptr;
};

class ElementList {
public:
    ElementList() = default;
    explicit ElementList(std::vector<Element> items) : m_items(std::move(items)) {}

    std::int64_t size() const { return static_cast<std::int64_t>(m_items.size()); }
    bool empty() const { return m_items.empty(); }
    Element at(std::int64_t index) const { return index >= 0 && index < size() ? m_items[static_cast<std::size_t>(index)] : Element(); }
    Element first() const { return at(0); }
    Element last() const { return at(size() - 1); }
    void push_back(const Element& e) { m_items.push_back(e); }

    static inline ElementList children_of(const Element& element);
    static inline ElementList query_all_in(const Element& element, const std::string& selector);

    std::vector<Element>::const_iterator begin() const { return m_items.begin(); }
    std::vector<Element>::const_iterator end() const { return m_items.end(); }

private:
    std::vector<Element> m_items;
};

namespace detail {

inline ElementList collect(grove_document* d, grove_element* scope, const std::string& selector) {
    const size_t count = grove_query_all(d, scope, selector.c_str(), nullptr, 0);
    std::vector<grove_element*> raw(count);
    if (count) grove_query_all(d, scope, selector.c_str(), raw.data(), raw.size());
    std::vector<Element> items;
    items.reserve(count);
    for (grove_element* e : raw) items.emplace_back(e);
    return ElementList(std::move(items));
}

inline std::string escape_selector_name(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return std::string();
        out += c;
    }
    return out;
}

} // namespace detail

inline Element Event::target() const { return Element(grove_event_target(m_event)); }
inline Element Event::current_target() const { return Element(grove_event_current_target(m_event)); }
inline ElementList Element::children() const {
    std::vector<Element> items;
    for (std::int64_t i = 0; i < child_count(); ++i) {
        Element c = child(i);
        if (!c.tag().empty() && c.tag()[0] != '#') items.push_back(c);
    }
    return ElementList(std::move(items));
}
inline ElementList Element::query_all(const std::string& selector) const { return detail::collect(doc(), m_element, selector); }
inline ElementList ElementList::children_of(const Element& element) { return element.children(); }
inline ElementList ElementList::query_all_in(const Element& element, const std::string& selector) { return element.query_all(selector); }

class Document {
public:
    Document() = default;
    explicit Document(grove_document* document) : m_document(document) {}

    static Document current() { return Document(grove_current_document()); }

    grove_document* handle() const { return m_document ? m_document : grove_current_document(); }

    Element root() const { return Element(grove_root(handle())); }
    Element body() const { return Element(grove_body(handle())); }

    Element get_by_id(const std::string& id) const { return Element(grove_find_by_id(handle(), id.c_str())); }
    ElementList get_by_class(const std::string& name) const {
        const std::string n = detail::escape_selector_name(name);
        return n.empty() ? ElementList() : detail::collect(handle(), nullptr, "." + n);
    }
    ElementList get_by_tag(const std::string& tag) const {
        const std::string n = detail::escape_selector_name(tag);
        return n.empty() ? ElementList() : detail::collect(handle(), nullptr, n);
    }
    Element id_selector(const std::string& id) const { return get_by_id(id); }
    ElementList class_selector(const std::string& name) const { return get_by_class(name); }
    ElementList tag_selector(const std::string& tag) const { return get_by_tag(tag); }

    Element query(const std::string& selector) const { return Element(grove_query(handle(), nullptr, selector.c_str())); }
    ElementList query_all(const std::string& selector) const { return detail::collect(handle(), nullptr, selector); }

    Element create_element(const std::string& tag) const { return Element(grove_create_element(handle(), tag.c_str())); }

    std::string choice(const std::string& group) const { return detail::str(grove_radio_choice(handle(), group.c_str())); }
    bool set_choice(const std::string& group, const std::string& value) const { return grove_set_radio_choice(handle(), group.c_str(), value.c_str()); }

    std::int64_t add_event_listener(const std::string& type, std::function<void(Event&)> fn) const {
        auto* boxed = new std::function<void(Event&)>(std::move(fn));
        return static_cast<std::int64_t>(grove_add_event_listener(handle(), nullptr, type.c_str(), &detail::listener_trampoline, boxed, &detail::listener_release));
    }
    std::int64_t on(const std::string& type, std::function<void(Event&)> fn) const { return add_event_listener(type, std::move(fn)); }
    std::int64_t listen(const std::string& type, std::function<void()> fn) const {
        auto* boxed = new std::function<void()>(std::move(fn));
        return static_cast<std::int64_t>(grove_add_event_listener(handle(), nullptr, type.c_str(), &detail::simple_trampoline, boxed, &detail::simple_release));
    }
    bool remove_event_listener(std::int64_t id) const { return grove_remove_event_listener(handle(), static_cast<uint64_t>(id)); }
    bool dispatch(const std::string& type) const { return grove_dispatch_event(handle(), nullptr, type.c_str()); }

    std::int64_t set_timeout(double milliseconds, std::function<void()> fn) const {
        auto* boxed = new std::function<void()>(std::move(fn));
        return static_cast<std::int64_t>(grove_set_timeout(handle(), milliseconds, &detail::timer_trampoline, boxed, &detail::timer_release));
    }
    std::int64_t set_interval(double milliseconds, std::function<void()> fn) const {
        auto* boxed = new std::function<void()>(std::move(fn));
        return static_cast<std::int64_t>(grove_set_interval(handle(), milliseconds, &detail::timer_trampoline, boxed, &detail::timer_release));
    }
    bool clear_timer(std::int64_t id) const { return grove_clear_timer(handle(), static_cast<uint64_t>(id)); }

    Element main_window() const { return Element(grove_main_window(handle())); }

    bool has_native(const std::string& name) const { return grove_has_native(handle(), name.c_str()); }
    std::string call_with(const std::string& name, const std::vector<std::string>& args) const {
        std::vector<const char*> raw;
        raw.reserve(args.size());
        for (const std::string& a : args) raw.push_back(a.c_str());
        return detail::str(grove_call_native(handle(), name.c_str(), raw.data(), raw.size()));
    }
    template <typename... Args>
    std::string call(const std::string& name, const Args&... args) const {
        return call_with(name, std::vector<std::string>{ std::string(args)... });
    }
    std::string choose_file(const std::string& mode, const std::string& title, const std::string& filter) const {
        return detail::str(grove_file_dialog(handle(), mode.c_str(), title.c_str(), filter.c_str(), false));
    }
    std::vector<std::string> choose_files(const std::string& title, const std::string& filter) const {
        const std::string joined = detail::str(grove_file_dialog(handle(), "open", title.c_str(), filter.c_str(), true));
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start < joined.size()) {
            const std::size_t end = joined.find('\n', start);
            out.push_back(joined.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return out;
    }

    Element popup(const std::string& kind, const std::string& title, const std::string& message, const std::string& input, const std::string& value, const std::string& options) const {
        return Element(grove_popup(handle(), nullptr, kind.c_str(), title.c_str(), message.c_str(), input.c_str(), value.c_str(), options.c_str()));
    }
    Element popup_info(const std::string& title, const std::string& message) const { return popup("info", title, message, "", "", ""); }
    Element popup_warning(const std::string& title, const std::string& message) const { return popup("warning", title, message, "", "", ""); }
    Element popup_error(const std::string& title, const std::string& message) const { return popup("error", title, message, "", "", ""); }
    Element alert(const std::string& message) const { return popup_info("", message); }
    Element warning(const std::string& message) const { return popup_warning("", message); }
    Element error(const std::string& message) const { return popup_error("", message); }

    Element confirm(const std::string& title, const std::string& message, std::function<void(bool)> fn) const {
        Element p = popup("question", title, message, "", "", "");
        p.listen("ok", [fn] { if (fn) fn(true); });
        p.listen("cancel", [fn] { if (fn) fn(false); });
        return p;
    }
    Element prompt_string(const std::string& title, const std::string& message, const std::string& value, std::function<void(std::string)> fn) const {
        Element p = popup("question", title, message, "string", value, "");
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_string()); });
        return p;
    }
    Element prompt_integer(const std::string& title, const std::string& message, std::int64_t value, std::function<void(std::int64_t)> fn) const {
        Element p = popup("question", title, message, "integer", std::to_string(value), "");
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_integer()); });
        return p;
    }
    Element prompt_float(const std::string& title, const std::string& message, double value, std::function<void(double)> fn) const {
        Element p = popup("question", title, message, "float", detail::number(value), "");
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_float()); });
        return p;
    }
    Element prompt_bool(const std::string& title, const std::string& message, bool value, std::function<void(bool)> fn) const {
        Element p = popup("question", title, message, "bool", value ? "true" : "false", "");
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_bool()); });
        return p;
    }
    Element prompt_color(const std::string& title, const std::string& message, const std::string& value, std::function<void(std::string)> fn) const {
        Element p = popup("question", title, message, "color", value, "");
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_color()); });
        return p;
    }
    Element prompt_choice(const std::string& title, const std::string& message, const std::string& options, const std::string& value, std::function<void(std::string)> fn) const {
        Element p = popup("question", title, message, "choice", value, options);
        p.listen("ok", [fn, p] { if (fn) fn(p.answer_value()); });
        return p;
    }
    Element prompt_slider(const std::string& title, const std::string& message, double value, double min, double max, std::function<void(double)> fn) const {
        Element p = popup("question", title, message, "slider", detail::number(value), detail::number(min) + "|" + detail::number(max));
        p.listen("ok", [fn, p] { if (fn) fn(p.query(".popup-input").value_float()); });
        return p;
    }

    double time_ms() const { return grove_time_ms(handle()); }
    double width() const { return grove_viewport_width(handle()); }
    double height() const { return grove_viewport_height(handle()); }
    void quit() const { grove_quit(handle()); }
    void log(const std::string& message) const { grove_log(message.c_str()); }

private:
    grove_document* m_document = nullptr;
};

inline Document document;

inline void log(const std::string& message) { grove_log(message.c_str()); }

namespace detail {

inline void listener_trampoline(grove_event* e, void* user) {
    try {
        Event event(e);
        (*static_cast<std::function<void(Event&)>*>(user))(event);
    } catch (const std::exception& ex) {
        report(ex.what());
    } catch (...) {
        report("an event listener threw an exception");
    }
}

inline void listener_release(void* user) { delete static_cast<std::function<void(Event&)>*>(user); }

inline void simple_trampoline(grove_event* e, void* user) {
    (void)e;
    try {
        (*static_cast<std::function<void()>*>(user))();
    } catch (const std::exception& ex) {
        report(ex.what());
    } catch (...) {
        report("an event listener threw an exception");
    }
}

inline void simple_release(void* user) { delete static_cast<std::function<void()>*>(user); }

inline void timer_trampoline(void* user) {
    try {
        (*static_cast<std::function<void()>*>(user))();
    } catch (const std::exception& ex) {
        report(ex.what());
    } catch (...) {
        report("a timer threw an exception");
    }
}

inline void timer_release(void* user) { delete static_cast<std::function<void()>*>(user); }

} // namespace detail

} // namespace grove

#define GROVE_ON_LOAD_JOIN2(a, b) a##b
#define GROVE_ON_LOAD_JOIN(a, b) GROVE_ON_LOAD_JOIN2(a, b)
#define GROVE_ON_LOAD(name) \
    static void name(); \
    static const ::grove::detail::OnLoadRegistrar GROVE_ON_LOAD_JOIN(grove_on_load_registrar_, name)(GROVE_SCRIPT_ID, &name); \
    static void name()

#endif // GROVE_DOM_HPP
