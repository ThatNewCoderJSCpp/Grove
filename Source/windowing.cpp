#include "grove/windowing.hpp"
#include "grove/dom_tools.hpp"
#include "grove/input/input.hpp"

#include <algorithm>
#include <cstdlib>

namespace grove {

namespace {

std::string normalize_kind(const std::string& kind) {
    const std::string k = normalize_event_type(kind);
    if (k == "warning" || k == "warn") return "warning";
    if (k == "error") return "error";
    if (k == "question" || k == "confirm") return "question";
    return "info";
}

std::string normalize_input(const std::string& input) {
    const std::string k = normalize_event_type(input);
    if (k == "string" || k == "text") return "string";
    if (k == "integer" || k == "int") return "integer";
    if (k == "float" || k == "number") return "float";
    if (k == "bool" || k == "boolean" || k == "switch") return k == "switch" ? "switch" : "bool";
    if (k == "color" || k == "colour") return "color";
    if (k == "choice" || k == "radio") return "choice";
    if (k == "slider" || k == "range") return "slider";
    return "";
}

std::string default_title(const std::string& kind) {
    if (kind == "warning") return "Warning";
    if (kind == "error") return "Error";
    if (kind == "question") return "Question";
    return "Message";
}

std::string glyph(const std::string& kind) {
    if (kind == "warning") return "!";
    if (kind == "error") return "\xC3\x97";
    if (kind == "question") return "?";
    return "i";
}

Node* add(Document& document, Node& parent, const std::string& tag, const std::string& cls = std::string(), const std::string& text = std::string()) {
    Node* n = document.create_element(tag);
    if (!cls.empty()) n->set_attribute("class", cls);
    if (!text.empty()) document.append_child(*n, *document.create_text(text));
    document.append_child(parent, *n);
    return n;
}

bool parse_size(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return false;
    const std::string rest(end);
    if (!rest.empty() && rest != "px") return false;
    out = v;
    return true;
}

unsigned g_popup_counter = 0;

} // namespace

bool is_window_element(const Node& node) {
    return node.is_element() && (node.tag() == "window" || node.tag() == "body");
}

bool is_popup(const Node& node) {
    return node.is_element() && node.tag() == "window" && node.has_attribute("popup");
}

Node* main_window(Document& document) {
    Node* main = document.host().main_window;
    if (main && document.is_attached(*main)) return main;
    if (Node* body = document.first_by_tag("body")) return body;
    if (Node* window = document.first_by_tag("window")) return window;
    return &document.root();
}

Node* window_of(Document& document, Node* node) {
    Node* main = main_window(document);
    for (Node* n = node; n; n = n->parent()) {
        if (n == main) return main;
        if (n->is_element() && n->tag() == "window") return n;
    }
    return main;
}

Node* owner_window(Document& document, Node& window) {
    if (&window == main_window(document)) return nullptr;
    return window_of(document, window.parent());
}

bool window_is_open(Document& document, const Node& window) {
    if (&window == main_window(document)) return true;
    return window.is_element() && window.tag() == "window" && document.is_attached(window) && window.has_attribute("open");
}

bool open_window(Document& document, Node& window) {
    if (&window == main_window(document)) return true;
    if (!window.is_element() || window.tag() != "window") return false;
    if (!window.has_attribute("open")) {
        window.set_attribute("open", "");
        document.touch();
    }
    return true;
}

bool close_window(Document& document, Node& window) {
    if (&window == main_window(document)) {
        document.events().request_quit();
        return true;
    }
    if (!window.is_element() || window.tag() != "window") return false;
    if (window.remove_attribute("open")) document.touch();
    return true;
}

bool window_size(Document& document, const Node& window, double& width, double& height) {
    width = 0.0;
    height = 0.0;
    const DocumentHost& host = document.host();
    if (host.window_size && host.window_size(window, width, height)) return true;
    if (&window == main_window(document) && host.viewport_width > 0.0) {
        width = host.viewport_width;
        height = host.viewport_height;
        return true;
    }
    const bool w = parse_size(window.attribute_or("width", ""), width);
    const bool h = parse_size(window.attribute_or("height", ""), height);
    return w || h;
}

std::vector<std::string> split_popup_options(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == '|') {
            out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty() || !out.empty()) out.push_back(current);
    return out;
}

Node* create_popup(Document& document, Node* owner, const PopupOptions& options) {
    Node* anchor = owner;
    if (!anchor) {
        if (Event* e = document.events().current_event()) anchor = e->target;
    }
    Node* parent = window_of(document, anchor);
    if (!parent) return nullptr;

    const std::string kind = normalize_kind(options.kind);
    const std::string input = normalize_input(options.input);
    const bool asks = !input.empty() || kind == "question";
    const std::string group = "grove-popup-" + std::to_string(++g_popup_counter);

    Node* popup = document.create_element("window");
    popup->set_attribute("class", "popup popup-" + kind);
    popup->set_attribute("popup", kind);
    popup->set_attribute("title", options.title.empty() ? default_title(kind) : options.title);
    popup->set_attribute("width", "420");
    if (!input.empty()) popup->set_attribute("input", input);
    if (input == "choice") popup->set_attribute("group", group);

    Node* main = add(document, *popup, "row", "popup-main");
    add(document, *main, "p", "popup-icon", glyph(kind));
    Node* text = add(document, *main, "column", "popup-text");
    std::string line;
    bool any_line = false;
    auto flush = [&] {
        if (!line.empty()) {
            add(document, *text, "p", "popup-message", line);
            any_line = true;
        }
        line.clear();
    };
    for (char c : options.message) {
        if (c == '\n') flush();
        else line += c;
    }
    flush();
    if (!any_line) add(document, *text, "p", "popup-message", " ");

    Node* field = nullptr;
    if (input == "string" || input == "integer" || input == "float" || input == "color" || input == "slider") {
        field = add(document, *text, "input_" + input, "popup-input");
        if (!options.value.empty()) field->set_attribute("value", options.value);
        if ((input == "integer" || input == "float" || input == "slider") && options.options.size() >= 2) {
            if (!options.options[0].empty()) field->set_attribute("min", options.options[0]);
            if (!options.options[1].empty()) field->set_attribute("max", options.options[1]);
            if (options.options.size() >= 3 && !options.options[2].empty()) field->set_attribute("step", options.options[2]);
        }
        field->set_attribute("autofocus", "");
        if (input == "string") {
            auto fit = [&document, popup, field](const std::string& text) {
                std::size_t points = 0;
                for (const unsigned char c : text) points += (c & 0xC0) != 0x80 ? 1 : 0;
                const long wanted = 160 + static_cast<long>(points) * 8;
                const long width = std::clamp<long>(wanted, 420, 760);
                const std::string value = std::to_string(width);
                if (popup->attribute_or("width", "") != value) {
                    popup->set_attribute("width", value);
                    field->set_attribute("style", "width: " + std::to_string(std::max<long>(300, width - 120)) + "px");
                    document.touch();
                }
            };
            fit(options.value);
            document.events().add_listener(field, "input", [fit](Event& e) { fit(e.edit_text); });
        }
    } else if (input == "bool" || input == "switch") {
        field = add(document, *text, "input_bool", "popup-input", options.options.empty() ? std::string() : options.options[0]);
        if (input == "switch") field->set_attribute("switch", "");
        const std::string v = normalize_event_type(options.value);
        if (v == "true" || v == "yes" || v == "on" || v == "1") field->set_attribute("checked", "");
    } else if (input == "choice") {
        Node* list = add(document, *text, "column", "popup-input popup-choices");
        bool checked = false;
        for (const std::string& o : options.options) {
            Node* radio = add(document, *list, "input_radio", "popup-choice", o);
            radio->set_attribute("name", group);
            radio->set_attribute("value", o);
            if (!checked && o == options.value) {
                radio->set_attribute("checked", "");
                checked = true;
            }
        }
        if (!checked && !list->children().empty()) list->children().front()->set_attribute("checked", "");
    }

    Node* buttons = add(document, *popup, "row", "popup-buttons");
    if (asks) add(document, *buttons, "button", "popup-cancel", "Cancel");
    Node* ok = add(document, *buttons, "button", "popup-ok", "OK");
    if (!field || input == "bool" || input == "switch" || input == "choice") ok->set_attribute("autofocus", "");

    document.events().add_listener(popup, "click", [&document, popup](Event& e) {
        for (Node* n = e.target; n && n != popup; n = n->parent()) {
            if (!n->is_element()) continue;
            if (n->has_class("popup-ok")) {
                answer_popup(document, *popup, true);
                return;
            }
            if (n->has_class("popup-cancel")) {
                answer_popup(document, *popup, false);
                return;
            }
        }
    });

    popup->set_attribute("open", "");
    document.append_child(*parent, *popup);
    return popup;
}

bool answer_popup(Document& document, Node& popup, bool ok) {
    if (!is_popup(popup) || popup.has_attribute("answered")) return false;
    popup.set_attribute("answered", ok ? "ok" : "cancel");
    std::string value;
    const std::string input = popup.attribute_or("input", "");
    if (input == "choice") {
        value = radio_choice(document, popup.attribute_or("group", "")).value_or("");
    } else if (Node* field = query(popup, ".popup-input")) {
        value = input_text(*field);
    }
    popup.set_attribute("value", value);
    document.touch();

    Event e;
    e.type = ok ? "ok" : "cancel";
    document.events().dispatch(document, &popup, e);

    if (popup.remove_attribute("open")) document.touch();
    if (document.is_attached(popup)) document.remove(popup);
    Node* p = &popup;
    document.events().set_timeout(0.0, [&document, p] {
        p->walk([&document](const Node& n) { document.events().remove_listeners(&n); });
    });
    return true;
}

std::vector<Node*> open_popups(Document& document, const Node& owner) {
    std::vector<Node*> out;
    for (Node* w : document.elements_by_tag("window")) {
        if (!is_popup(*w) || !w->has_attribute("open") || w->has_attribute("answered")) continue;
        if (owner_window(document, *w) == &owner) out.push_back(w);
    }
    return out;
}

MenuItem menu_item(const std::string& value, const std::string& label, const std::string& detail, bool disabled) {
    MenuItem item;
    item.value = value;
    item.label = label;
    item.detail = detail;
    item.disabled = disabled;
    return item;
}

MenuItem menu_separator() {
    MenuItem item;
    item.separator = true;
    return item;
}

bool open_menu(Document& document, Node& owner, const std::vector<MenuItem>& items, double x, double y) {
    if (items.empty() || !document.host().open_menu) return false;
    return document.host().open_menu(owner, items, x, y);
}

bool close_menu(Document& document) {
    return document.host().close_menu && document.host().close_menu();
}

std::string selected_text(Document& document) {
    return document.host().selected_text ? document.host().selected_text() : std::string();
}

bool select_text(Document& document, Node& node) {
    return document.host().select_text && document.host().select_text(node);
}

void clear_text_selection(Document& document) {
    if (document.host().clear_selection) document.host().clear_selection();
}

} // namespace grove
