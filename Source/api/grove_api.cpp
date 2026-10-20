#define GROVE_API_BUILDING
#include "grove/api/grove_host.h"
#include "grove/markup/node.hpp"
#include "grove/input/input.hpp"
#include "grove/dom_tools.hpp"
#include "grove/windowing.hpp"
#include "grove/native.hpp"
#include "grove/pages.hpp"

#include <cstdlib>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

grove::Document* doc_of(grove_document* d) noexcept { return reinterpret_cast<grove::Document*>(d); }
const grove::Document* doc_of(const grove_document* d) noexcept { return reinterpret_cast<const grove::Document*>(d); }
grove::Node* node_of(grove_element* e) noexcept { return reinterpret_cast<grove::Node*>(e); }
const grove::Node* node_of(const grove_element* e) noexcept { return reinterpret_cast<const grove::Node*>(e); }
grove_element* handle(grove::Node* n) noexcept { return reinterpret_cast<grove_element*>(n); }

grove_document* g_current = nullptr;

int kind_code(const grove::Node* node) noexcept {
    if (!node) return GROVE_INPUT_NONE;
    const std::optional<grove::InputKind> k = grove::input_kind_of(*node);
    if (!k) return GROVE_INPUT_NONE;
    switch (*k) {
        case grove::InputKind::Integer: return GROVE_INPUT_INTEGER;
        case grove::InputKind::Float: return GROVE_INPUT_FLOAT;
        case grove::InputKind::String: return GROVE_INPUT_STRING;
        case grove::InputKind::Bool: return GROVE_INPUT_BOOL;
        case grove::InputKind::Radio: return GROVE_INPUT_RADIO;
        case grove::InputKind::Slider: return GROVE_INPUT_SLIDER;
        case grove::InputKind::Color: return GROVE_INPUT_COLOR;
        case grove::InputKind::Code: return GROVE_INPUT_CODE;
        case grove::InputKind::File: return GROVE_INPUT_FILE;
        case grove::InputKind::Select: return GROVE_INPUT_SELECT;
    }
    return GROVE_INPUT_NONE;
}

uint32_t pack(const grove::bark::Rgba& c) noexcept {
    return (static_cast<uint32_t>(c.r) << 24) | (static_cast<uint32_t>(c.g) << 16) | (static_cast<uint32_t>(c.b) << 8) | static_cast<uint32_t>(c.a);
}

grove::bark::Rgba unpack(uint32_t v) noexcept {
    return grove::bark::Rgba{ static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v) };
}

grove::Node* by_id(const char* id) noexcept {
    return g_current && id ? doc_of(g_current)->find_by_id(id) : nullptr;
}

grove::Event* event_of(grove_event* e) noexcept { return reinterpret_cast<grove::Event*>(e); }
const grove::Event* event_of(const grove_event* e) noexcept { return reinterpret_cast<const grove::Event*>(e); }

const char* keep(std::string value) {
    thread_local std::array<std::string, 8> ring;
    thread_local std::size_t next = 0;
    std::string& slot = ring[next];
    next = (next + 1) % ring.size();
    slot = std::move(value);
    return slot.c_str();
}

struct Release {
    void* user_data = nullptr;
    grove_release_fn release = nullptr;
    ~Release() {
        if (release) release(user_data);
    }
};

} // namespace

extern "C" {

grove_element* grove_root(grove_document* document) {
    return document ? handle(&doc_of(document)->root()) : nullptr;
}

grove_element* grove_find_by_id(grove_document* document, const char* id) {
    return document && id ? handle(doc_of(document)->find_by_id(id)) : nullptr;
}

grove_element* grove_first_by_tag(grove_document* document, const char* tag) {
    return document && tag ? handle(doc_of(document)->first_by_tag(tag)) : nullptr;
}

const char* grove_tag(const grove_element* element) {
    return element ? node_of(element)->tag().c_str() : "";
}

grove_element* grove_parent(grove_element* element) {
    return element ? handle(node_of(element)->parent()) : nullptr;
}

size_t grove_child_count(const grove_element* element) {
    return element ? node_of(element)->children().size() : 0;
}

grove_element* grove_child(grove_element* element, size_t index) {
    if (!element) return nullptr;
    const auto& children = node_of(element)->children();
    return index < children.size() ? handle(children[index].get()) : nullptr;
}

int grove_has_attribute(const grove_element* element, const char* name) {
    return element && name && node_of(element)->has_attribute(name) ? 1 : 0;
}

const char* grove_get_attribute(const grove_element* element, const char* name) {
    if (!element || !name) return nullptr;
    const grove::Attribute* a = node_of(element)->find_attribute(name);
    return a ? a->value.c_str() : nullptr;
}

void grove_set_attribute(grove_document* document, grove_element* element, const char* name, const char* value) {
    if (!element || !name) return;
    node_of(element)->set_attribute(name, value ? value : "");
    if (document) doc_of(document)->touch();
}

int grove_remove_attribute(grove_document* document, grove_element* element, const char* name) {
    if (!element || !name) return 0;
    const bool removed = node_of(element)->remove_attribute(name);
    if (removed && document) doc_of(document)->touch();
    return removed ? 1 : 0;
}

size_t grove_get_text(const grove_element* element, char* buffer, size_t size) {
    if (!element) return 0;
    const std::string text = node_of(element)->inner_text();
    if (buffer && size > 0) {
        const size_t n = text.size() < size - 1 ? text.size() : size - 1;
        std::memcpy(buffer, text.data(), n);
        buffer[n] = '\0';
    }
    return text.size();
}

void grove_set_text(grove_document* document, grove_element* element, const char* text) {
    if (!element) return;
    node_of(element)->set_inner_text(text ? text : "");
    if (document) doc_of(document)->touch();
}

unsigned long long grove_revision(const grove_document* document) {
    return document ? static_cast<unsigned long long>(doc_of(document)->revision()) : 0ULL;
}

void grove_bind_document(grove_document* document) {
    g_current = document;
}

grove_document* grove_current_document(void) {
    return g_current;
}

int grove_set_text_by_id(const char* id, const char* text) {
    grove_element* e = grove_find_by_id(g_current, id);
    if (!e) return 0;
    grove_set_text(g_current, e, text);
    return 1;
}

const char* grove_get_attribute_by_id(const char* id, const char* name) {
    return grove_get_attribute(grove_find_by_id(g_current, id), name);
}

int grove_set_attribute_by_id(const char* id, const char* name, const char* value) {
    grove_element* e = grove_find_by_id(g_current, id);
    if (!e) return 0;
    grove_set_attribute(g_current, e, name, value);
    return 1;
}

int grove_input_kind_of(const grove_element* element) {
    return kind_code(node_of(element));
}

bool grove_input_integer(const grove_element* element, int64_t* out) {
    if (!element) return false;
    const auto v = grove::input_integer(*node_of(element));
    if (v && out) *out = *v;
    return v.has_value();
}

bool grove_input_float(const grove_element* element, double* out) {
    if (!element) return false;
    const auto v = grove::input_float(*node_of(element));
    if (v && out) *out = *v;
    return v.has_value();
}

bool grove_input_bool(const grove_element* element, bool* out) {
    if (!element) return false;
    const auto v = grove::input_bool(*node_of(element));
    if (v && out) *out = *v;
    return v.has_value();
}

const char* grove_input_string(const grove_element* element) {
    if (!element) return nullptr;
    const std::optional<grove::InputKind> kind = grove::input_kind_of(*node_of(element));
    if (!kind || !grove::is_string_input(*kind)) return nullptr;
    if (*kind == grove::InputKind::Code && !node_of(element)->has_attribute("value")) return keep(grove::code_source_text(*node_of(element)));
    const grove::Attribute* a = node_of(element)->find_attribute("value");
    return a ? a->value.c_str() : "";
}

bool grove_input_color(const grove_element* element, uint32_t* rgba) {
    if (!element) return false;
    const auto v = grove::input_color(*node_of(element));
    if (v && rgba) *rgba = pack(*v);
    return v.has_value();
}

bool grove_set_input_integer(grove_document* document, grove_element* element, int64_t value) {
    return document && element && grove::set_input_integer(*doc_of(document), *node_of(element), value);
}

bool grove_set_input_float(grove_document* document, grove_element* element, double value) {
    return document && element && grove::set_input_float(*doc_of(document), *node_of(element), value);
}

bool grove_set_input_bool(grove_document* document, grove_element* element, bool value) {
    return document && element && grove::set_input_bool(*doc_of(document), *node_of(element), value);
}

bool grove_set_input_string(grove_document* document, grove_element* element, const char* value) {
    return document && element && grove::set_input_string(*doc_of(document), *node_of(element), value ? value : "");
}

bool grove_set_input_color(grove_document* document, grove_element* element, uint32_t rgba) {
    return document && element && grove::set_input_color(*doc_of(document), *node_of(element), unpack(rgba));
}

const char* grove_radio_choice(grove_document* document, const char* group) {
    if (!document || !group) return nullptr;
    const char* out = nullptr;
    doc_of(document)->root().walk([&](const grove::Node& n) {
        if (out || grove::input_kind_of(n) != grove::InputKind::Radio || !n.has_attribute("checked")) return;
        if (n.attribute_or("name", "") != group) return;
        const grove::Attribute* v = n.find_attribute("value");
        out = v ? v->value.c_str() : "";
    });
    return out;
}

bool grove_set_radio_choice(grove_document* document, const char* group, const char* value) {
    return document && group && value && grove::set_radio_choice(*doc_of(document), group, value);
}

int grove_input_kind_by_id(const char* id) {
    return kind_code(by_id(id));
}

int64_t grove_get_integer_by_id(const char* id) {
    const grove::Node* n = by_id(id);
    return n ? grove::input_integer(*n).value_or(0) : 0;
}

double grove_get_float_by_id(const char* id) {
    const grove::Node* n = by_id(id);
    return n ? grove::input_float(*n).value_or(0.0) : 0.0;
}

bool grove_get_bool_by_id(const char* id) {
    const grove::Node* n = by_id(id);
    return n ? grove::input_bool(*n).value_or(false) : false;
}

const char* grove_get_string_by_id(const char* id) {
    const char* s = grove_input_string(handle(by_id(id)));
    return s ? s : "";
}

uint32_t grove_get_color_by_id(const char* id) {
    uint32_t v = 0;
    grove_input_color(handle(by_id(id)), &v);
    return v;
}

bool grove_set_integer_by_id(const char* id, int64_t value) {
    return grove_set_input_integer(g_current, handle(by_id(id)), value);
}

bool grove_set_float_by_id(const char* id, double value) {
    return grove_set_input_float(g_current, handle(by_id(id)), value);
}

bool grove_set_bool_by_id(const char* id, bool value) {
    return grove_set_input_bool(g_current, handle(by_id(id)), value);
}

bool grove_set_string_by_id(const char* id, const char* value) {
    return grove_set_input_string(g_current, handle(by_id(id)), value);
}

bool grove_set_color_by_id(const char* id, uint32_t rgba) {
    return grove_set_input_color(g_current, handle(by_id(id)), rgba);
}

const char* grove_get_choice(const char* group) {
    const char* s = grove_radio_choice(g_current, group);
    return s ? s : "";
}

bool grove_set_choice(const char* group, const char* value) {
    return grove_set_radio_choice(g_current, group, value);
}

grove_element* grove_body(grove_document* document) {
    if (!document) return nullptr;
    grove::Document& d = *doc_of(document);
    if (grove::Node* body = d.first_by_tag("body")) return handle(body);
    if (grove::Node* window = d.first_by_tag("window")) return handle(window);
    return handle(&d.root());
}

grove_element* grove_query(grove_document* document, grove_element* scope, const char* selector) {
    if (!document || !selector) return nullptr;
    grove::Node& root = scope ? *node_of(scope) : doc_of(document)->root();
    return handle(grove::query(root, selector));
}

size_t grove_query_all(grove_document* document, grove_element* scope, const char* selector, grove_element** out, size_t capacity) {
    if (!document || !selector) return 0;
    grove::Node& root = scope ? *node_of(scope) : doc_of(document)->root();
    const std::vector<grove::Node*> found = grove::query_all(root, selector);
    for (size_t i = 0; out && i < found.size() && i < capacity; ++i) out[i] = handle(found[i]);
    return found.size();
}

const char* grove_id(const grove_element* element) {
    if (!element) return "";
    const grove::Attribute* a = node_of(element)->find_attribute("id");
    return a ? a->value.c_str() : "";
}

const char* grove_text(const grove_element* element) {
    return element ? keep(node_of(element)->inner_text()) : "";
}

size_t grove_index_in_parent(const grove_element* element) {
    return element ? node_of(element)->index_in_parent() : 0;
}

bool grove_is_attached(grove_document* document, const grove_element* element) {
    return document && element && doc_of(document)->is_attached(*node_of(element));
}

grove_element* grove_create_element(grove_document* document, const char* tag) {
    if (!document || !tag || !*tag) return nullptr;
    return handle(doc_of(document)->create_element(tag));
}

bool grove_append_child(grove_document* document, grove_element* parent, grove_element* child) {
    return document && parent && child && doc_of(document)->append_child(*node_of(parent), *node_of(child));
}

bool grove_insert_before(grove_document* document, grove_element* parent, grove_element* child, grove_element* reference) {
    return document && parent && child && doc_of(document)->insert_before(*node_of(parent), *node_of(child), reference ? node_of(reference) : nullptr);
}

bool grove_remove_element(grove_document* document, grove_element* element) {
    return document && element && doc_of(document)->remove(*node_of(element));
}

bool grove_has_class(const grove_element* element, const char* name) {
    return element && name && node_of(element)->has_class(name);
}

void grove_add_class(grove_document* document, grove_element* element, const char* name) {
    if (document && element && name) grove::add_class(*doc_of(document), *node_of(element), name);
}

void grove_remove_class(grove_document* document, grove_element* element, const char* name) {
    if (document && element && name) grove::remove_class(*doc_of(document), *node_of(element), name);
}

bool grove_toggle_class(grove_document* document, grove_element* element, const char* name) {
    return document && element && name && grove::toggle_class(*doc_of(document), *node_of(element), name);
}

const char* grove_get_style(const grove_element* element, const char* property) {
    if (!element || !property) return "";
    const std::optional<std::string> v = grove::inline_style(*node_of(element), property);
    return v ? keep(*v) : "";
}

void grove_set_style(grove_document* document, grove_element* element, const char* property, const char* value) {
    if (document && element && property) grove::set_inline_style(*doc_of(document), *node_of(element), property, value ? value : "");
}

const char* grove_computed_style(grove_document* document, grove_element* element, const char* property) {
    if (!document || !element || !property) return "";
    const grove::DocumentHost& host = doc_of(document)->host();
    if (!host.computed_style) return grove_get_style(element, property);
    return keep(host.computed_style(*node_of(element), property));
}

bool grove_has_computed_style(grove_document* document, grove_element* element, const char* property) {
    const char* text = grove_computed_style(document, element, property);
    return text && *text;
}

double grove_computed_number_at(grove_document* document, grove_element* element, const char* property, size_t index, double fallback) {
    if (!document || !element || !property) return fallback;
    const grove::DocumentHost& host = doc_of(document)->host();
    double out = fallback;
    if (host.computed_number && host.computed_number(*node_of(element), property, index, out)) return out;
    if (index == 0) {
        const char* text = grove_get_style(element, property);
        char* end = nullptr;
        const double v = text ? std::strtod(text, &end) : 0.0;
        if (text && end != text) return v;
    }
    return fallback;
}

double grove_computed_number(grove_document* document, grove_element* element, const char* property, double fallback) {
    return grove_computed_number_at(document, element, property, 0, fallback);
}

const char* grove_computed_color(grove_document* document, grove_element* element, const char* property) {
    if (!document || !element || !property) return "";
    const grove::DocumentHost& host = doc_of(document)->host();
    if (!host.computed_color) return "";
    return keep(host.computed_color(*node_of(element), property));
}

bool grove_focus(grove_document* document, grove_element* element) {
    if (!document) return false;
    const grove::DocumentHost& host = doc_of(document)->host();
    return host.focus && host.focus(element ? node_of(element) : nullptr);
}

bool grove_has_state(const grove_element* element, const char* state) {
    if (!element || !state) return false;
    const std::string s = grove::normalize_event_type(state);
    if (s == "hover") return node_of(element)->has_state(grove::StateHover);
    if (s == "active") return node_of(element)->has_state(grove::StateActive);
    if (s == "focus") return node_of(element)->has_state(grove::StateFocus);
    return false;
}

uint64_t grove_add_event_listener(grove_document* document, grove_element* element, const char* type, grove_listener_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    if (!document || !type || !fn) return 0;
    return doc_of(document)->events().add_listener(element ? node_of(element) : nullptr, type, [fn, guard](grove::Event& e) {
        fn(reinterpret_cast<grove_event*>(&e), guard->user_data);
    });
}

bool grove_remove_event_listener(grove_document* document, uint64_t id) {
    return document && doc_of(document)->events().remove_listener(id);
}

bool grove_dispatch_event(grove_document* document, grove_element* target, const char* type) {
    if (!document || !type) return false;
    grove::Event e;
    e.type = type;
    return doc_of(document)->events().dispatch(*doc_of(document), target ? node_of(target) : nullptr, e);
}

grove_event* grove_current_event(grove_document* document) {
    return document ? reinterpret_cast<grove_event*>(doc_of(document)->events().current_event()) : nullptr;
}

const char* grove_event_type(const grove_event* event) { return event ? event_of(event)->type.c_str() : ""; }
grove_element* grove_event_target(grove_event* event) { return event ? handle(event_of(event)->target) : nullptr; }
grove_element* grove_event_current_target(grove_event* event) { return event ? handle(event_of(event)->current_target) : nullptr; }
double grove_event_x(const grove_event* event) { return event ? event_of(event)->x : 0.0; }
double grove_event_y(const grove_event* event) { return event ? event_of(event)->y : 0.0; }
double grove_event_delta_x(const grove_event* event) { return event ? event_of(event)->delta_x : 0.0; }
double grove_event_delta_y(const grove_event* event) { return event ? event_of(event)->delta_y : 0.0; }
int grove_event_button(const grove_event* event) { return event ? event_of(event)->button : 0; }
const char* grove_event_key(const grove_event* event) { return event ? event_of(event)->key.c_str() : ""; }
const char* grove_event_text(const grove_event* event) { return event ? event_of(event)->text.c_str() : ""; }
unsigned grove_event_modifiers(const grove_event* event) { return event ? event_of(event)->modifiers : 0u; }
void grove_event_stop_propagation(grove_event* event) { if (event) event_of(event)->propagation_stopped = true; }
void grove_event_prevent_default(grove_event* event) { if (event) event_of(event)->default_prevented = true; }
bool grove_event_default_prevented(const grove_event* event) { return event && event_of(event)->default_prevented; }

uint64_t grove_set_timeout(grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    if (!document || !fn) return 0;
    return doc_of(document)->events().set_timeout(milliseconds, [fn, guard]() { fn(guard->user_data); });
}

uint64_t grove_set_interval(grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    if (!document || !fn) return 0;
    return doc_of(document)->events().set_interval(milliseconds, [fn, guard]() { fn(guard->user_data); });
}

bool grove_clear_timer(grove_document* document, uint64_t id) {
    return document && doc_of(document)->events().clear_timer(id);
}

double grove_time_ms(grove_document* document) {
    return document ? doc_of(document)->events().now() : 0.0;
}

double grove_viewport_width(grove_document* document) {
    return document ? doc_of(document)->host().viewport_width : 0.0;
}

double grove_viewport_height(grove_document* document) {
    return document ? doc_of(document)->host().viewport_height : 0.0;
}

void grove_quit(grove_document* document) {
    if (document) doc_of(document)->events().request_quit();
}

void grove_log(const char* message) {
    std::cerr << "script: " << (message ? message : "") << '\n';
}

grove_element* grove_main_window(grove_document* document) {
    return document ? handle(grove::main_window(*doc_of(document))) : nullptr;
}

grove_element* grove_window_of(grove_document* document, grove_element* element) {
    return document ? handle(grove::window_of(*doc_of(document), element ? node_of(element) : nullptr)) : nullptr;
}

bool grove_window_open(grove_document* document, grove_element* window) {
    return document && window && grove::open_window(*doc_of(document), *node_of(window));
}

bool grove_window_close(grove_document* document, grove_element* window) {
    return document && window && grove::close_window(*doc_of(document), *node_of(window));
}

bool grove_window_is_open(grove_document* document, grove_element* window) {
    return document && window && grove::window_is_open(*doc_of(document), *node_of(window));
}

double grove_window_width(grove_document* document, grove_element* window) {
    if (!document || !window) return 0.0;
    double w = 0.0;
    double h = 0.0;
    grove::window_size(*doc_of(document), *node_of(window), w, h);
    return w;
}

double grove_window_height(grove_document* document, grove_element* window) {
    if (!document || !window) return 0.0;
    double w = 0.0;
    double h = 0.0;
    grove::window_size(*doc_of(document), *node_of(window), w, h);
    return h;
}

bool grove_window_resize(grove_document* document, grove_element* window, double width, double height) {
    if (!document || !window || width < 1.0 || height < 1.0) return false;
    grove::Document& d = *doc_of(document);
    grove::Node& n = *node_of(window);
    if (d.host().resize_window && d.host().resize_window(n, width, height)) return true;
    if (n.tag() != "window") return false;
    n.set_attribute("width", std::to_string(static_cast<long long>(width)));
    n.set_attribute("height", std::to_string(static_cast<long long>(height)));
    d.touch();
    return true;
}

bool grove_window_move(grove_document* document, grove_element* window, double x, double y) {
    if (!document || !window) return false;
    grove::Document& d = *doc_of(document);
    return d.host().move_window && d.host().move_window(*node_of(window), x, y);
}

bool grove_window_focus(grove_document* document, grove_element* window) {
    if (!document || !window) return false;
    grove::Document& d = *doc_of(document);
    return d.host().focus_window && d.host().focus_window(*node_of(window));
}

grove_element* grove_popup(grove_document* document, grove_element* owner, const char* kind, const char* title, const char* message, const char* input, const char* value, const char* options) {
    if (!document) return nullptr;
    grove::PopupOptions o;
    o.kind = kind ? kind : "info";
    o.title = title ? title : "";
    o.message = message ? message : "";
    o.input = input ? input : "";
    o.value = value ? value : "";
    o.options = grove::split_popup_options(options ? options : "");
    return handle(grove::create_popup(*doc_of(document), owner ? node_of(owner) : nullptr, o));
}

bool grove_popup_answer(grove_document* document, grove_element* popup, bool ok) {
    return document && popup && grove::answer_popup(*doc_of(document), *node_of(popup), ok);
}

const char* grove_input_text(const grove_element* element) {
    if (!element || !grove::input_kind_of(*node_of(element))) return "";
    return keep(grove::input_text(*node_of(element)));
}

const char* grove_call_native(grove_document* document, const char* name, const char* const* args, size_t count) {
    if (!document || !name) return nullptr;
    std::vector<std::string> list;
    list.reserve(count);
    for (size_t i = 0; i < count; ++i) list.emplace_back(args && args[i] ? args[i] : "");
    const std::optional<std::string> result = grove::call_native(*doc_of(document), name, list);
    return result ? keep(*result) : nullptr;
}

bool grove_has_native(grove_document* document, const char* name) {
    return document && name && grove::has_native(*doc_of(document), name);
}

const char* grove_file_dialog(grove_document* document, const char* mode, const char* title, const char* filter, bool multiple) {
    if (!document) return "";
    grove::FileDialogRequest request;
    request.mode = mode && *mode ? mode : "open";
    request.title = title ? title : "";
    request.filter = filter ? filter : "";
    request.multiple = multiple;
    const std::vector<std::string> paths = grove::run_file_dialog(*doc_of(document), request);
    std::string joined;
    for (const std::string& p : paths) {
        if (!joined.empty()) joined += '\n';
        joined += p;
    }
    return keep(joined);
}

bool grove_show_page(grove_document* document, grove_element* page) {
    return document && page && grove::show_page(*doc_of(document), *node_of(page));
}

bool grove_go_back(grove_document* document) {
    return document && grove::go_back(*doc_of(document));
}

bool grove_open_modal(grove_document* document, grove_element* modal) {
    return document && modal && grove::open_modal(*doc_of(document), *node_of(modal));
}

bool grove_close_modal(grove_document* document, grove_element* modal) {
    return document && modal && grove::close_modal(*doc_of(document), *node_of(modal));
}

bool grove_navigate(grove_document* document, const char* target) {
    return document && target && grove::navigate(*doc_of(document), target);
}

bool grove_is_current(const grove_element* element) {
    return element && grove::is_navigation_current(*node_of(element));
}

const grove_host_api* grove_host_table(void) {
    static const grove_host_api table = [] {
        grove_host_api api{};
        api.version = GROVE_HOST_API_VERSION;
        api.size = static_cast<uint32_t>(sizeof(grove_host_api));
        api.document = nullptr;
#define GROVE_HOST_FILL(ret, name, params, args) api.name = &::name;
        GROVE_API_FUNCTIONS(GROVE_HOST_FILL)
#undef GROVE_HOST_FILL
        return api;
    }();
    return &table;
}

}
