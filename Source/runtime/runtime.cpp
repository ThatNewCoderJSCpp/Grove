#include "grove/runtime/runtime.hpp"
#include "grove/markup/tags.hpp"
#include "grove/api/grove_api.h"
#include "grove/input/input.hpp"
#include "grove/script/loader.hpp"
#include "grove/windowing.hpp"

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <thread>
#include <unordered_map>

namespace grove {
namespace runtime {

namespace {

using fizmo::colors::Color;

Color to_color(const bark::Rgba& c) { return Color(c.r, c.g, c.b, c.a); }

struct Box {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
};

Box box_of(const bark::ComputedStyle& style, const std::string& property) {
    const bark::ValueList* v = style.get(property);
    Box b;
    if (!v || v->empty()) return b;
    const double fs = style.font_size();
    auto at = [&](std::size_t i) { return static_cast<float>(bark::as_length(*v, fs, style.viewport(), i).value_or(0.0)); };
    switch (v->size()) {
        case 1: b.top = b.right = b.bottom = b.left = at(0); break;
        case 2: b.top = b.bottom = at(0); b.left = b.right = at(1); break;
        case 3: b.top = at(0); b.left = b.right = at(1); b.bottom = at(2); break;
        default: b.top = at(0); b.right = at(1); b.bottom = at(2); b.left = at(3); break;
    }
    return b;
}

std::string collapse_whitespace(const std::string& text) {
    std::string out;
    bool space = false;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

Color mix(const Color& a, const Color& b, float t) {
    auto ch = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
    return Color(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()), ch(a.alpha(), b.alpha()));
}

Color with_alpha(Color c, double factor) {
    return Color(c.red(), c.green(), c.blue(), static_cast<std::uint8_t>(std::lround(c.alpha() * factor)));
}

void pop_code_point(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) return;
    }
}

std::vector<std::string> split_code_points(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        n = std::min(n, s.size() - i);
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

struct DrawOp {
    enum class Kind { Fill, Stroke, Circle, CircleStroke, Text, Math } kind = Kind::Fill;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    float radius = 0;
    Color color;
    unsigned stroke = 1;
    std::string text;
    fizmo::text::TextStyle style;
    const fizmo::text::FancyText* math = nullptr;
};

struct Hit {
    const Node* node = nullptr;
    InputKind kind = InputKind::String;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    float track_x = 0;
    float track_w = 0;

    bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct MathEntry {
    std::string key;
    fizmo::text::FancyText text;
};

struct ElementBox {
    const Node* node = nullptr;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
};

fizmo::windows::SystemCursor cursor_for(const std::string& name) {
    using fizmo::windows::SystemCursor;
    if (name == "pointer" || name == "hand") return SystemCursor::Hand;
    if (name == "text") return SystemCursor::IBeam;
    if (name == "crosshair") return SystemCursor::Crosshair;
    if (name == "wait") return SystemCursor::Wait;
    if (name == "progress") return SystemCursor::Progress;
    if (name == "not-allowed") return SystemCursor::NotAllowed;
    if (name == "move") return SystemCursor::Move;
    if (name == "help") return SystemCursor::Help;
    return SystemCursor::Arrow;
}

unsigned modifiers_of(fizmo::input::Modifiers m) {
    using fizmo::input::Modifiers;
    unsigned out = 0;
    if (fizmo::input::has(m, Modifiers::Shift)) out |= ModifierShift;
    if (fizmo::input::has(m, Modifiers::Control)) out |= ModifierControl;
    if (fizmo::input::has(m, Modifiers::Alt)) out |= ModifierAlt;
    if (fizmo::input::has(m, Modifiers::Meta)) out |= ModifierMeta;
    return out;
}

bool is_disabled(const Node* n) {
    for (; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute("disabled")) return true;
    }
    return false;
}

class DocumentView {
public:
    DocumentView(Project& project, bark::StyleEngine& styles, const Node& window, bool main) : m_project(project), m_styles(styles), m_window(window), m_main(main) {}

    void set_blocked(std::function<bool()> blocked, std::function<void()> on_blocked) {
        m_blocked = std::move(blocked);
        m_on_blocked = std::move(on_blocked);
    }

    const Node& root() const noexcept { return m_window; }
    float content_height() const noexcept { return m_content_height; }

    void apply_autofocus() {
        const Node* found = nullptr;
        std::function<void(const Node&)> visit = [&](const Node& n) {
            if (found) return;
            for (const auto& c : n.children()) {
                if (!c->is_element() || c->tag() == "window") continue;
                if (c->has_attribute("autofocus") && !is_disabled(c.get())) {
                    found = c.get();
                    return;
                }
                visit(*c);
                if (found) return;
            }
        };
        visit(m_window);
        if (found) focus_from_script(const_cast<Node*>(found));
    }

    void release_states() {
        for (const Node* n : m_hover) mutable_node(*n).set_state(StateHover, false);
        for (const Node* n : m_active) mutable_node(*n).set_state(StateActive, false);
        if (m_focused) mutable_node(*m_focused).set_state(StateFocus, false);
        m_hover.clear();
        m_active.clear();
        m_focused = nullptr;
        m_focus = nullptr;
        m_styles.invalidate();
    }

    void set_window(fizmo::windows::Window* window) noexcept { m_native = window; }
    void set_title_callback(std::function<void(const std::string&)> fn) { m_on_title = std::move(fn); }

    Document& document() { return m_project.document; }

    std::string title_now() {
        const bark::ComputedStyle& style = style_of(m_window);
        std::string title = m_window.attribute_or("title", style.text("title").value_or(""));
        if (title.empty()) {
            if (const Node* t = document().first_by_tag("title")) title = collapse_whitespace(t->inner_text());
        }
        return title.empty() ? std::string("Grove") : title;
    }

    void paint(fizmo::graphics::Renderer& r) {
        const auto now = std::chrono::steady_clock::now();
        m_dt = m_has_frame ? std::min(0.1f, std::chrono::duration<float>(now - m_last_frame).count()) : 0.0f;
        m_last_frame = now;
        m_has_frame = true;

        const float w = static_cast<float>(r.viewport_width());
        const float h = static_cast<float>(r.viewport_height());
        if (w != m_view_w || h != m_view_h) {
            const bool first = m_view_w == 0 && m_view_h == 0;
            m_view_w = w;
            m_view_h = h;
            if (m_main) {
                document().host().viewport_width = w;
                document().host().viewport_height = h;
            }
            m_styles.set_viewport(bark::Viewport{ w, h });
            if (!first) {
                Event e;
                e.type = "resize";
                e.bubbles = !m_main;
                e.x = w;
                e.y = h;
                dispatch(m_main ? nullptr : &m_window, e);
            }
        }
        if (m_revision != document().revision()) {
            m_revision = document().revision();
            m_styles.invalidate();
            const std::string title = title_now();
            if (title != m_title) {
                m_title = title;
                if (m_on_title) m_on_title(title);
            }
            if (m_focus && !document().is_attached(*m_focus)) blur(false);
        }

        m_ops.clear();
        m_hits.clear();
        m_boxes.clear();
        m_opacity.clear();
        const bark::ComputedStyle& style = style_of(m_window);
        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, 0, 0, w, h, to_color(*bg));
        const Box pad = box_of(style, "padding");
        float y = pad.top - m_scroll;
        const float start = y;
        lay_content(r, m_window, style, pad.left, y, w - pad.left - pad.right);
        m_content_height = (y - start) + pad.top + pad.bottom;
        const float max_scroll = std::max(0.0f, m_content_height - h);
        if (m_scroll > max_scroll) m_scroll = max_scroll;
        for (const DrawOp& op : m_ops) draw(r, op);
    }

    void on_event(const fizmo::windows::WindowEvent& e) {
        using fizmo::windows::WindowEventType;
        m_last_activity = std::chrono::steady_clock::now();
        if (m_blocked && m_blocked()) {
            if (e.type == WindowEventType::MouseClick && m_on_blocked) m_on_blocked();
            if (e.type == WindowEventType::MouseClick || e.type == WindowEventType::MouseRelease || e.type == WindowEventType::MouseDoubleClick || e.type == WindowEventType::KeyPress || e.type == WindowEventType::KeyRelease || e.type == WindowEventType::TextInput || e.type == WindowEventType::MouseScroll) return;
            if (e.type == WindowEventType::MouseMove) {
                m_mouse_x = static_cast<float>(e.x);
                m_mouse_y = static_cast<float>(e.y);
                return;
            }
        }
        m_mods = modifiers_of(e.mods);
        const float px = static_cast<float>(e.x);
        const float py = static_cast<float>(e.y);
        switch (e.type) {
            case WindowEventType::MouseMove:
                m_mouse_x = px;
                m_mouse_y = py;
                if (m_dragging) drag_to(px);
                hover_at(px, py);
                if (document().events().has_listeners("mouse_move")) {
                    Event ev = pointer_event("mouse_move", px, py, 0);
                    dispatch(hit_element(px, py), ev);
                }
                break;
            case WindowEventType::MouseClick:
                if (e.button == 1) mouse_down(px, py);
                else {
                    Event ev = pointer_event("mouse_down", px, py, static_cast<int>(e.button));
                    dispatch(hit_element(px, py), ev);
                }
                break;
            case WindowEventType::MouseRelease:
                mouse_up(px, py, static_cast<int>(e.button));
                break;
            case WindowEventType::MouseDoubleClick: {
                Event ev = pointer_event("double_click", px, py, static_cast<int>(e.button));
                dispatch(hit_element(px, py), ev);
                break;
            }
            case WindowEventType::MouseScroll: {
                const double dy = e.scroll_y != 0.0f ? static_cast<double>(e.scroll_y) : static_cast<double>(e.scroll_delta);
                Event ev = pointer_event("wheel", m_mouse_x, m_mouse_y, 0);
                ev.delta_x = e.scroll_x;
                ev.delta_y = dy;
                if (dispatch(hit_element(m_mouse_x, m_mouse_y), ev)) {
                    m_scroll = std::max(0.0f, m_scroll - static_cast<float>(dy) * 48.0f);
                    hover_at(m_mouse_x, m_mouse_y);
                }
                break;
            }
            case WindowEventType::TextInput: type_text(e.text); break;
            case WindowEventType::KeyPress: key_down(e); break;
            case WindowEventType::KeyRelease: {
                Event ev = key_event("key_up", e);
                dispatch(key_target(), ev);
                break;
            }
            default: break;
        }
    }

    bool focus_from_script(Node* node) {
        if (!node) {
            set_focused(nullptr);
            return true;
        }
        if (!document().is_attached(*node) || is_disabled(node)) return false;
        const std::optional<InputKind> kind = input_kind_of(*node);
        if (kind && is_text_input(*kind)) focus(node);
        else set_focused(node);
        return true;
    }

    std::string computed(const Node& node, const std::string& property) {
        const bark::ValueList* v = style_of(node).get(property);
        return v ? bark::to_string(*v) : std::string();
    }

private:
    const bark::ComputedStyle& style_of(const Node& node) { return m_styles.style_of(node, m_project.diagnostics); }
    Node& mutable_node(const Node& node) { return const_cast<Node&>(node); }

    bool dispatch(const Node* target, Event& e) {
        e.modifiers = m_mods;
        const std::uint64_t before = document().revision();
        const bool result = document().events().dispatch(document(), const_cast<Node*>(target), e);
        if (document().revision() != before) m_styles.invalidate();
        return result;
    }

    Event pointer_event(const char* type, float x, float y, int button) {
        Event e;
        e.type = type;
        e.x = x;
        e.y = y;
        e.button = button;
        return e;
    }

    Event key_event(const char* type, const fizmo::windows::WindowEvent& w) {
        Event e;
        e.type = type;
        e.key = w.key_name;
        e.text = w.text;
        return e;
    }

    const Node* key_target() {
        if (m_focused && document().is_attached(*m_focused)) return m_focused;
        return &m_window;
    }

    DrawOp& push(DrawOp::Kind kind, float x, float y, float w, float h, const Color& color) {
        DrawOp op;
        op.kind = kind;
        op.x = x;
        op.y = y;
        op.w = w;
        op.h = h;
        op.color = color;
        m_ops.push_back(std::move(op));
        return m_ops.back();
    }

    std::size_t push_rect(DrawOp::Kind kind, float x, float y, float w, float h, const Color& color, unsigned stroke = 1, float radius = 0) {
        DrawOp& op = push(kind, x, y, w, h, color);
        op.stroke = stroke;
        op.radius = radius;
        return m_ops.size() - 1;
    }

    void push_text(float x, float y, float w, float h, std::string text, const fizmo::text::TextStyle& style) {
        DrawOp& op = push(DrawOp::Kind::Text, x, y, w, h, Color());
        op.text = std::move(text);
        op.style = style;
    }

    static void draw(fizmo::graphics::Renderer& r, const DrawOp& op) {
        const int x = static_cast<int>(std::lround(op.x));
        const int y = static_cast<int>(std::lround(op.y));
        const unsigned w = static_cast<unsigned>(std::max(0.0f, op.w));
        const unsigned h = static_cast<unsigned>(std::max(0.0f, op.h));
        switch (op.kind) {
            case DrawOp::Kind::Fill:
                if (op.radius > 0) r.draw_rounded_rect(op.x, op.y, op.w, op.h, op.radius, fizmo::graphics::Paint::fill(op.color));
                else r.draw_rect(x, y, w, h, fizmo::graphics::Paint::fill(op.color));
                break;
            case DrawOp::Kind::Stroke:
                if (op.radius > 0) r.draw_rounded_rect(op.x, op.y, op.w, op.h, op.radius, fizmo::graphics::Paint::stroke(op.color, op.stroke));
                else r.draw_rect(x, y, w, h, fizmo::graphics::Paint::stroke(op.color, op.stroke));
                break;
            case DrawOp::Kind::Circle: r.draw_circle(x, y, static_cast<unsigned>(std::lround(op.radius)), fizmo::graphics::Paint::fill(op.color)); break;
            case DrawOp::Kind::CircleStroke: r.draw_circle(x, y, static_cast<unsigned>(std::lround(op.radius)), fizmo::graphics::Paint::stroke(op.color, op.stroke)); break;
            case DrawOp::Kind::Text: r.draw_text(x, y, std::max(w, 1u), std::max(h, 1u), op.text, op.style); break;
            case DrawOp::Kind::Math: if (op.math) r.draw_fancy_text(*op.math, op.x, op.y); break;
        }
    }

    double font_size_of(const Node& element) { return style_of(element).font_size(); }

    double opacity_of(const Node& element) {
        auto it = m_opacity.find(&element);
        if (it != m_opacity.end()) return it->second;
        double own = std::clamp(style_of(element).length("opacity").value_or(1.0), 0.0, 1.0);
        if (element.parent() && &element != &m_window) own *= opacity_of(*element.parent());
        m_opacity.emplace(&element, own);
        return own;
    }

    Color raw_text_color(const Node& element) { return to_color(style_of(element).color("color").value_or(bark::Rgba{ 230, 230, 230, 255 })); }

    Color text_color(const Node& element) { return with_alpha(to_color(style_of(element).color("color").value_or(bark::Rgba{ 230, 230, 230, 255 })), opacity_of(element)); }
    Color accent_color(const Node& element) { return to_color(style_of(element).color("accent-color").value_or(bark::Rgba{ 137, 180, 250, 255 })); }

    fizmo::text::TextStyle text_style(const Node& element) {
        const bark::ComputedStyle& style = style_of(element);
        fizmo::text::TextStyle ts(font_size_of(element), text_color(element));
        if (style.text("font-weight").value_or("") == "bold") ts.set_bold(true);
        return ts;
    }

    float text_width(fizmo::graphics::Renderer& r, const std::string& text, const fizmo::text::TextStyle& style) {
        if (text.empty()) return 0.0f;
        return static_cast<float>(r.measure_text(text.c_str(), static_cast<int>(text.size()), style).width);
    }

    float line_height(fizmo::graphics::Renderer& r, const fizmo::text::TextStyle& style) {
        const fizmo::text::TextMetrics m = r.measure_text("Hg", 2, style);
        return static_cast<float>(std::max(m.height, 1u));
    }

    bool is_row(const Node& element, const bark::ComputedStyle& style) {
        const std::string display = style.text("display").value_or("");
        return display == "row" || (display.empty() && element.tag() == "row");
    }

    void lay_content(fizmo::graphics::Renderer& r, const Node& element, const bark::ComputedStyle& style, float x, float& y, float width) {
        if (is_row(element, style)) layout_row(r, element, style, x, y, width);
        else layout_children(r, element, x, y, width);
    }

    void layout_children(fizmo::graphics::Renderer& r, const Node& parent, float x, float& y, float width) {
        for (const auto& child : parent.children()) {
            if (child->is_text()) layout_text(r, parent, child->text(), x, y, width);
            else if (child->is_element()) layout_element(r, *child, x, y, width);
        }
    }

    float natural_width(fizmo::graphics::Renderer& r, const Node& owner, const Node& node) {
        if (node.is_text()) return text_width(r, collapse_whitespace(node.text()), text_style(owner)) + 1.0f;
        if (!node.is_element()) return 0.0f;
        const bark::ComputedStyle& style = style_of(node);
        if (style.text("display").value_or("") == "none") return 0.0f;
        const Box margin = box_of(style, "margin");
        const float margins = margin.left + margin.right;
        if (const std::optional<InputKind> kind = input_kind_of(node)) {
            if (is_text_input(*kind)) return static_cast<float>(style.length("width").value_or(220.0)) + margins;
            if (*kind == InputKind::Slider) return static_cast<float>(style.length("width").value_or(220.0)) + 70.0f + margins;
            const float size = std::round(static_cast<float>(font_size_of(node)) * 1.05f);
            const bool toggle = *kind == InputKind::Bool && is_switch(node, style.text("appearance"));
            const float box = toggle ? std::round(size * 1.8f) : size;
            const std::string label = collapse_whitespace(node.inner_text());
            return box + (label.empty() ? 0.0f : 8.0f + text_width(r, label, text_style(node))) + margins + 2.0f;
        }
        if (const auto w = style.length("width")) return static_cast<float>(*w) + margins;
        if (is_inline_block(node, style)) {
            const Box pad = box_of(style, "padding");
            return text_width(r, collapse_whitespace(node.inner_text()), text_style(node)) + pad.left + pad.right + margins + 2.0f;
        }
        return -1.0f;
    }

    bool is_inline_block(const Node& node, const bark::ComputedStyle& style) {
        const std::string display = style.text("display").value_or("");
        return display == "inline-block" || (display.empty() && node.tag() == "button");
    }

    void layout_row(fizmo::graphics::Renderer& r, const Node& element, const bark::ComputedStyle& style, float x, float& y, float width) {
        const float gap = static_cast<float>(style.length("gap").value_or(8.0));
        std::vector<const Node*> items;
        for (const auto& c : element.children()) {
            if (c->is_text() && collapse_whitespace(c->text()).empty()) continue;
            if (c->is_element() && (c->tag() == "window" || style_of(*c).text("display").value_or("") == "none")) continue;
            if (c->is_element() || c->is_text()) items.push_back(c.get());
        }
        if (items.empty()) return;
        std::vector<float> widths;
        float fixed = 0.0f;
        std::size_t flexible = 0;
        for (const Node* n : items) {
            const float w = natural_width(r, element, *n);
            widths.push_back(w);
            if (w < 0.0f) ++flexible;
            else fixed += w;
        }
        const float spacing = gap * static_cast<float>(items.size() - 1);
        const float share = flexible ? std::max(0.0f, (width - fixed - spacing) / static_cast<float>(flexible)) : 0.0f;
        float cx = x;
        float step_gap = gap;
        if (!flexible) {
            const float free = std::max(0.0f, width - fixed - spacing);
            const std::string justify = style.text("justify-content").value_or("start");
            if (justify == "end" || justify == "flex-end" || justify == "right") cx = x + free;
            else if (justify == "center") cx = x + free / 2.0f;
            else if (justify == "space-between" && items.size() > 1) step_gap = gap + free / static_cast<float>(items.size() - 1);
        }
        float bottom = y;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const float w = widths[i] < 0.0f ? share : std::min(widths[i], std::max(0.0f, x + width - cx));
            float cy = y;
            if (items[i]->is_text()) layout_text(r, element, items[i]->text(), cx, cy, w);
            else layout_element(r, *items[i], cx, cy, w, true);
            bottom = std::max(bottom, cy);
            cx += w + step_gap;
        }
        y = bottom;
    }

    void layout_text(fizmo::graphics::Renderer& r, const Node& owner, const std::string& raw, float x, float& y, float width) {
        const std::string text = collapse_whitespace(raw);
        if (text.empty()) return;
        const fizmo::text::TextStyle style = text_style(owner);
        const unsigned w = static_cast<unsigned>(std::max(1.0f, width));
        const fizmo::text::TextMetrics m = r.measure_text(text, style, w);
        const float h = static_cast<float>(std::max(m.height, 1u));
        float tx = x;
        const std::string align = style_of(owner).text("text-align").value_or("left");
        if (align == "center" || align == "right") {
            const float tw = text_width(r, text, style);
            if (tw < width) tx = align == "center" ? x + (width - tw) / 2.0f : x + width - tw;
        }
        push_text(tx, y, static_cast<float>(w) - (tx - x), h, text, style);
        y += h;
    }

    void layout_math(const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        const Color color = to_color(style.color("math-color").value_or(style.color("color").value_or(bark::Rgba{ 230, 230, 230, 255 })));
        const float size = static_cast<float>(font_size_of(element));
        const std::string source = math_source_of(element);
        const bool display = element.attribute_or("display", "inline") == "block";
        const bool brace = math_syntax_of(element, m_project.path, m_project.diagnostics) == MathSyntax::Brace;
        const std::string key = source + '\x1f' + std::to_string(size) + '\x1f' + std::to_string(color.red()) + ',' + std::to_string(color.green()) + ',' + std::to_string(color.blue()) + ',' + std::to_string(color.alpha()) + (display ? "d" : "i") + (brace ? "b" : "x");
        MathEntry& entry = m_math[&element];
        if (entry.key != key) {
            fizmo::text::MathRenderOptions options = fizmo::text::detail::simple_math_options(size, color);
            options.brace_syntax = brace;
            options.layout.display = display;
            entry.text = fizmo::text::make_fancy_math(source, options);
            entry.key = key;
        }
        const fizmo::text::FancyText& ft = entry.text;
        float dx = x;
        const std::string align = style.text("text-align").value_or("left");
        if (align == "center") dx = x + std::max(0.0f, (width - ft.width()) / 2.0f);
        else if (align == "right") dx = x + std::max(0.0f, width - ft.width());
        DrawOp& op = push(DrawOp::Kind::Math, dx, y, 0, 0, Color());
        op.math = &ft;
        y += ft.height();
    }

    float box_width(const bark::ComputedStyle& style, float available) {
        const auto w = style.length("width");
        return w ? std::min(static_cast<float>(*w), available) : available;
    }

    void layout_text_input(fizmo::graphics::Renderer& r, const Node& element, InputKind kind, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        const Box pad = box_of(style, "padding");
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        const float w = box_width(style, width);
        const float h = lh + pad.top + pad.bottom;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
        const bool focused = m_focus == &element;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);

        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, w, h, with_alpha(to_color(*bg), fade), 1, radius);
        Color border = to_color(style.color("border-color").value_or(bark::Rgba{ 255, 255, 255, 60 }));
        if (focused) border = m_invalid ? Color(243, 139, 168, 255) : accent_color(element);
        push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(border, fade), focused ? 2u : 1u, radius);

        float tx = x + pad.left;
        if (kind == InputKind::Color) {
            const float sw = lh - 4;
            const bark::Rgba c = input_color(element).value_or(bark::Rgba{});
            push_rect(DrawOp::Kind::Fill, tx, y + pad.top + 2, sw, sw, to_color(c), 1, 3);
            push_rect(DrawOp::Kind::Stroke, tx, y + pad.top + 2, sw, sw, with_alpha(text_color(element), 0.4), 1, 3);
            tx += sw + 8;
        }
        const float tw = std::max(1.0f, x + w - pad.right - tx);
        const std::string text = focused ? m_edit : input_text(element);
        if (text.empty() && element.has_attribute("placeholder")) {
            fizmo::text::TextStyle muted = ts;
            muted.set_fg_color(with_alpha(text_color(element), 0.45));
            push_text(tx, y + pad.top, tw, lh, element.attribute_or("placeholder", ""), muted);
        } else {
            fizmo::text::TextStyle shown = ts;
            if (disabled) shown.set_fg_color(with_alpha(raw_text_color(element), fade));
            push_text(tx, y + pad.top, tw, lh, text, shown);
        }
        if (focused) {
            const float cx = std::min(tx + text_width(r, text, ts) + 1, x + w - pad.right);
            push_rect(DrawOp::Kind::Fill, cx, y + pad.top + 2, 2, lh - 4, accent_color(element));
        }
        if (!disabled) m_hits.push_back(Hit{ &element, kind, x, y, w, h, 0, 0 });
        m_boxes.push_back(ElementBox{ &element, x, y, w, h });
        y += h;
    }

    void layout_check(fizmo::graphics::Renderer& r, const Node& element, InputKind kind, float x, float& y, float width) {
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        const float size = std::round(static_cast<float>(font_size_of(element)) * 1.05f);
        const bool on = input_bool(element).value_or(false);
        const bool toggle = kind == InputKind::Bool && is_switch(element, style_of(element).text("appearance"));
        const float box = toggle ? std::round(size * 1.8f) : size;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);
        const Color accent = with_alpha(accent_color(element), fade);
        const Color outline = with_alpha(raw_text_color(element), 0.55 * fade);
        const float row = std::max(lh, size);
        const float by = y + (row - size) / 2.0f;

        if (kind == InputKind::Radio) {
            const float cx = x + size / 2.0f;
            const float cy = by + size / 2.0f;
            DrawOp& ring = push(DrawOp::Kind::CircleStroke, cx, cy, 0, 0, on ? accent : outline);
            ring.radius = size / 2.0f - 1;
            ring.stroke = 2;
            if (on) push(DrawOp::Kind::Circle, cx, cy, 0, 0, accent).radius = size / 4.0f;
        } else if (toggle) {
            const float target = on ? 1.0f : 0.0f;
            auto found = m_switch_pos.find(&element);
            if (found == m_switch_pos.end()) found = m_switch_pos.emplace(&element, target).first;
            float& pos = found->second;
            const float speed = m_dt / 0.12f;
            pos = pos < target ? std::min(target, pos + speed) : std::max(target, pos - speed);
            const Color background = to_color(style_of(m_window).color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
            const Color off_track = with_alpha(raw_text_color(element), 0.22 * fade);
            push_rect(DrawOp::Kind::Fill, x, by, box, size, mix(off_track, accent, pos), 1, size / 2.0f);
            const float knob = size / 2.0f - 3.0f;
            const float kx = x + size / 2.0f + pos * (box - size);
            push(DrawOp::Kind::Circle, kx, by + size / 2.0f, 0, 0, mix(with_alpha(raw_text_color(element), fade), background, pos)).radius = knob;
        } else {
            if (on) {
                push_rect(DrawOp::Kind::Fill, x, by, size, size, accent, 1, 4);
                fizmo::text::TextStyle mark(size * 0.8, to_color(style_of(m_window).color("background").value_or(bark::Rgba{ 30, 30, 46, 255 })));
                mark.set_bold(true);
                const std::string check = "\xE2\x9C\x93";
                const float mw = text_width(r, check, mark);
                const float mh = line_height(r, mark);
                push_text(x + (size - mw) / 2.0f, by + (size - mh) / 2.0f, mw + 2, mh, check, mark);
            } else {
                push_rect(DrawOp::Kind::Stroke, x, by, size, size, outline, 2, 4);
            }
        }

        const std::string label = collapse_whitespace(element.inner_text());
        float w = box;
        if (!label.empty()) {
            const float lx = x + box + 8;
            const float lw = std::max(1.0f, width - box - 8);
            fizmo::text::TextStyle shown = ts;
            if (disabled) shown.set_fg_color(with_alpha(raw_text_color(element), fade));
            push_text(lx, y + (row - lh) / 2.0f, lw, lh, label, shown);
            w = std::min(width, box + 8 + text_width(r, label, ts));
        }
        if (!disabled) m_hits.push_back(Hit{ &element, kind, x, y, w, row, 0, 0 });
        m_boxes.push_back(ElementBox{ &element, x, y, w, row });
        y += row;
    }

    void layout_slider(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        const float fs = static_cast<float>(font_size_of(element));
        const float row = std::max(lh, fs * 1.4f);
        const float thumb = fs * 0.55f;
        const float w = box_width(style, width);
        const float track_x = x + thumb;
        const float track_w = std::max(1.0f, w - 2 * thumb);
        const InputRange range = input_range(element);
        const double value = input_float(element).value_or(range.min);
        const double t = range.max > range.min ? (value - range.min) / (range.max - range.min) : 0.0;
        const float vx = track_x + static_cast<float>(t) * track_w;
        const float cy = y + row / 2.0f;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);
        const Color accent = with_alpha(accent_color(element), fade);

        push_rect(DrawOp::Kind::Fill, track_x, cy - 2, track_w, 4, with_alpha(raw_text_color(element), 0.2 * fade), 1, 2);
        push_rect(DrawOp::Kind::Fill, track_x, cy - 2, vx - track_x, 4, accent, 1, 2);
        push(DrawOp::Kind::Circle, vx, cy, 0, 0, accent).radius = thumb;

        const std::string shown = input_text(element);
        const float avail = width - w - 10;
        if (avail > 20) push_text(x + w + 10, y + (row - lh) / 2.0f, avail, lh, shown, ts);
        if (!disabled) m_hits.push_back(Hit{ &element, InputKind::Slider, x, y, w, row, track_x, track_w });
        m_boxes.push_back(ElementBox{ &element, x, y, w, row });
        y += row;
    }

    void layout_element(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, bool sized = false) {
        if (element.tag() == "window") return;
        const bark::ComputedStyle& style = style_of(element);
        if (style.text("display").value_or("") == "none") return;

        const Box margin = box_of(style, "margin");
        y += margin.top;
        const float bx = x + margin.left;
        float bw = width - margin.left - margin.right;

        if (const std::optional<InputKind> kind = input_kind_of(element)) {
            if (is_text_input(*kind)) layout_text_input(r, element, *kind, bx, y, bw);
            else if (*kind == InputKind::Slider) layout_slider(r, element, bx, y, bw);
            else layout_check(r, element, *kind, bx, y, bw);
            y += margin.bottom;
            if (auto gap = style.length("gap")) y += static_cast<float>(*gap);
            return;
        }

        if (!sized) {
            if (const auto w = style.length("width")) bw = std::min(bw, static_cast<float>(*w));
            else if (is_inline_block(element, style)) {
                const float natural = natural_width(r, *element.parent(), element) - margin.left - margin.right;
                if (natural > 0.0f) bw = std::min(bw, natural);
            }
        }

        const Box pad = box_of(style, "padding");
        const float content_x = bx + pad.left;
        const float content_w = bw - pad.left - pad.right;
        const float top = y;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
        const std::size_t box_index = m_boxes.size();
        m_boxes.push_back(ElementBox{ &element, bx, top, bw, 0 });

        const auto bg = style.color("background");
        const auto border = style.color("border-color");
        const double opacity = opacity_of(element);
        const std::size_t bg_op = bg ? push_rect(DrawOp::Kind::Fill, bx, top, bw, 0, with_alpha(to_color(*bg), opacity), 1, radius) : std::size_t(-1);

        float cy = y + pad.top;
        if (element.tag() == "math") layout_math(element, content_x, cy, content_w);
        else if (element.tag() == "br") cy += static_cast<float>(font_size_of(element));
        else if (element.tag() == "hr") {
            push_rect(DrawOp::Kind::Fill, content_x, cy + 4, content_w, 1, to_color(style.color("color").value_or(bark::Rgba{ 120, 120, 120, 255 })));
            cy += 9;
        } else lay_content(r, element, style, content_x, cy, content_w);

        float bottom = cy + pad.bottom;
        if (const auto hgt = style.length("height")) bottom = std::max(bottom, top + static_cast<float>(*hgt));
        if (const auto mh = style.length("min-height")) bottom = std::max(bottom, top + static_cast<float>(*mh));
        if (bg) m_ops[bg_op].h = bottom - top;
        if (border) push_rect(DrawOp::Kind::Stroke, bx, top, bw, bottom - top, with_alpha(to_color(*border), opacity), static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);
        m_boxes[box_index].h = bottom - top;
        y = bottom + margin.bottom;
        if (auto gap = style.length("gap")) {
            if (!is_row(element, style)) y += static_cast<float>(*gap);
        }
    }

    const Node* hit_element(float px, float py) const {
        for (auto it = m_boxes.rbegin(); it != m_boxes.rend(); ++it) {
            if (it->contains(px, py)) return it->node;
        }
        return &m_window;
    }

    void update_chain(std::vector<const Node*>& chain, const Node* target, unsigned bit, const char* enter, const char* leave) {
        std::vector<const Node*> next;
        for (const Node* n = target; n; n = n->parent()) {
            next.push_back(n);
            if (n == &m_window) break;
        }
        if (next == chain) return;
        std::vector<const Node*> left;
        std::vector<const Node*> entered;
        for (const Node* n : chain) {
            if (std::find(next.begin(), next.end(), n) == next.end()) left.push_back(n);
        }
        for (const Node* n : next) {
            if (std::find(chain.begin(), chain.end(), n) == chain.end()) entered.push_back(n);
        }
        for (const Node* n : left) mutable_node(*n).set_state(bit, false);
        for (const Node* n : entered) mutable_node(*n).set_state(bit, true);
        chain = std::move(next);
        m_styles.invalidate();
        if (enter && leave) {
            for (const Node* n : left) {
                Event e = pointer_event(leave, m_mouse_x, m_mouse_y, 0);
                e.bubbles = false;
                dispatch(n, e);
            }
            for (auto it = entered.rbegin(); it != entered.rend(); ++it) {
                Event e = pointer_event(enter, m_mouse_x, m_mouse_y, 0);
                e.bubbles = false;
                dispatch(*it, e);
            }
        }
    }

    void hover_at(float px, float py) {
        const Node* target = hit_element(px, py);
        update_chain(m_hover, target, StateHover, "mouse_enter", "mouse_leave");
        update_cursor(target);
    }

    void update_cursor(const Node* target) {
        if (!m_native || !target) return;
        std::string name = style_of(*target).text("cursor").value_or("");
        if (name.empty() || name == "auto") {
            const std::optional<InputKind> kind = input_kind_of(*target);
            name = kind && is_text_input(*kind) ? "text" : "default";
        }
        if (is_disabled(target)) name = "default";
        if (name == m_cursor) return;
        m_cursor = name;
        m_native->set_cursor(cursor_for(name));
    }

    void set_focused(const Node* node) {
        if (node == m_focused) return;
        const Node* old = m_focused;
        if (old) mutable_node(*old).set_state(StateFocus, false);
        m_focused = node;
        if (node) mutable_node(*node).set_state(StateFocus, true);
        m_styles.invalidate();
        if (old) {
            Event e;
            e.type = "blur";
            e.bubbles = false;
            dispatch(old, e);
        }
        if (node) {
            Event e;
            e.type = "focus";
            e.bubbles = false;
            dispatch(node, e);
        }
    }

    void fire_value(const Node& node, bool changed_too) {
        Event input;
        input.type = "input";
        dispatch(&node, input);
        if (changed_too) {
            Event change;
            change.type = "change";
            dispatch(&node, change);
        }
    }

    void focus(const Node* node) {
        if (m_focus == node) return;
        blur(true);
        m_focus = node;
        m_edit = input_text(*node);
        m_focus_start = m_edit;
        m_invalid = false;
        if (m_native) m_native->start_text_input();
        set_focused(node);
    }

    bool commit_focus() {
        if (!m_focus) return true;
        Node& node = mutable_node(*m_focus);
        if (!commit_input_text(document(), node, m_edit)) return false;
        m_edit = input_text(node);
        if (m_edit != m_focus_start) {
            m_focus_start = m_edit;
            Event change;
            change.type = "change";
            dispatch(&node, change);
        }
        return true;
    }

    void blur(bool commit) {
        if (!m_focus) return;
        if (commit) commit_focus();
        m_focus = nullptr;
        m_edit.clear();
        m_invalid = false;
        if (m_native) m_native->stop_text_input();
    }

    void mouse_down(float px, float py) {
        const Node* target = hit_element(px, py);
        m_pressed = target;
        update_chain(m_active, target, StateActive, nullptr, nullptr);
        Event down = pointer_event("mouse_down", px, py, 1);
        const bool allowed = dispatch(target, down);

        if (allowed) {
            for (const Hit& hit : m_hits) {
                if (!hit.contains(px, py)) continue;
                Node& node = mutable_node(*hit.node);
                switch (hit.kind) {
                    case InputKind::Bool:
                        blur(true);
                        set_focused(hit.node);
                        set_input_bool(document(), node, !input_bool(node).value_or(false));
                        fire_value(node, true);
                        return;
                    case InputKind::Radio:
                        blur(true);
                        set_focused(hit.node);
                        if (!input_bool(node).value_or(false)) {
                            set_input_bool(document(), node, true);
                            fire_value(node, true);
                        }
                        return;
                    case InputKind::Slider:
                        blur(true);
                        set_focused(hit.node);
                        m_dragging = hit.node;
                        m_drag_start = input_text(node);
                        m_drag_track_x = hit.track_x;
                        m_drag_track_w = hit.track_w;
                        drag_to(px);
                        return;
                    default:
                        focus(hit.node);
                        return;
                }
            }
        }
        blur(true);
        const Node* focusable = nullptr;
        for (const Node* n = target; n; n = n->parent()) {
            if (n->is_element() && (n->tag() == "button" || n->has_attribute("tabindex"))) {
                focusable = n;
                break;
            }
        }
        set_focused(focusable);
    }

    void mouse_up(float px, float py, int button) {
        const Node* target = hit_element(px, py);
        Event up = pointer_event("mouse_up", px, py, button);
        dispatch(target, up);
        if (button != 1) return;
        update_chain(m_active, nullptr, StateActive, nullptr, nullptr);
        if (m_dragging) {
            const Node* dragged = m_dragging;
            m_dragging = nullptr;
            if (input_text(*dragged) != m_drag_start) {
                Event change;
                change.type = "change";
                dispatch(dragged, change);
            }
        }
        const Node* pressed = m_pressed;
        m_pressed = nullptr;
        if (!pressed || !target) return;
        const Node* common = nullptr;
        for (const Node* a = target; a && !common; a = a->parent()) {
            if (pressed->is_inside(a)) common = a;
        }
        if (!common || is_disabled(common)) return;
        Event click = pointer_event("click", px, py, 1);
        dispatch(common, click);
    }

    void drag_to(float px) {
        if (!m_dragging) return;
        const InputRange range = input_range(*m_dragging);
        const double t = std::clamp((px - m_drag_track_x) / std::max(1.0f, m_drag_track_w), 0.0f, 1.0f);
        Node& node = mutable_node(*m_dragging);
        const std::string before = input_text(node);
        set_input_float(document(), node, range.min + t * (range.max - range.min));
        if (input_text(node) != before) fire_value(node, false);
    }

    void type_text(const std::string& text) {
        Event e;
        e.type = "text";
        e.text = text;
        if (!dispatch(key_target(), e)) return;
        if (!m_focus) return;
        bool changed = false;
        for (const std::string& cp : split_code_points(text)) {
            if (input_accepts(*m_focus, m_edit, cp)) {
                m_edit += cp;
                m_invalid = false;
                changed = true;
            }
        }
        if (changed) {
            Event input;
            input.type = "input";
            input.text = text;
            dispatch(m_focus, input);
        }
    }

    void focus_next(int direction) {
        std::vector<const Node*> order;
        for (const Hit& h : m_hits) {
            if (is_text_input(h.kind)) order.push_back(h.node);
        }
        if (order.empty()) return;
        auto it = std::find(order.begin(), order.end(), m_focus);
        std::size_t index = 0;
        if (it != order.end()) {
            const std::size_t at = static_cast<std::size_t>(it - order.begin());
            index = (at + order.size() + static_cast<std::size_t>(direction > 0 ? 1 : order.size() - 1)) % order.size();
        }
        if (m_focus && !commit_focus()) m_edit = input_text(*m_focus);
        const Node* next = order[index];
        blur(false);
        focus(next);
    }

    void key_down(const fizmo::windows::WindowEvent& e) {
        using fizmo::input::Key;
        Event ev = key_event("key_down", e);
        if (!dispatch(key_target(), ev)) return;
        const Key key = e.logical_key != Key::Unknown ? e.logical_key : e.physical_key;
        if (is_popup(m_window) && (key == Key::Enter || key == Key::Escape)) {
            Node& popup = mutable_node(m_window);
            if (key == Key::Escape) {
                blur(false);
                answer_popup(document(), popup, false);
                return;
            }
            if (m_focus) {
                if (!commit_focus()) {
                    m_invalid = true;
                    return;
                }
                blur(false);
                answer_popup(document(), popup, true);
                return;
            }
            if (!(m_focused && m_focused->tag() == "button")) {
                answer_popup(document(), popup, true);
                return;
            }
        }
        if (!m_focus) {
            if (m_focused && (key == Key::Enter || key == Key::Space) && m_focused->tag() == "button" && !is_disabled(m_focused)) {
                Event click;
                click.type = "click";
                dispatch(m_focused, click);
            }
            return;
        }
        Node& node = mutable_node(*m_focus);
        switch (key) {
            case Key::Backspace:
                if (!m_edit.empty()) {
                    pop_code_point(m_edit);
                    m_invalid = false;
                    Event input;
                    input.type = "input";
                    dispatch(m_focus, input);
                }
                break;
            case Key::Enter:
                if (commit_focus()) blur(false);
                else m_invalid = true;
                break;
            case Key::Escape:
                blur(false);
                break;
            case Key::Tab:
                focus_next(fizmo::input::has(e.mods, fizmo::input::Modifiers::Shift) ? -1 : 1);
                break;
            case Key::Up:
            case Key::Down:
                if (commit_focus() || input_kind_of(node) != InputKind::String) {
                    if (step_input(document(), node, key == Key::Up ? 1 : -1)) {
                        m_edit = input_text(node);
                        m_invalid = false;
                        fire_value(node, false);
                        commit_focus();
                    }
                }
                break;
            default:
                break;
        }
    }

    Project& m_project;
    bark::StyleEngine& m_styles;
    const Node& m_window;
    bool m_main = true;
    std::function<bool()> m_blocked;
    std::function<void()> m_on_blocked;
    fizmo::windows::Window* m_native = nullptr;
    std::function<void(const std::string&)> m_on_title;
    std::string m_title;
    std::uint64_t m_revision = ~std::uint64_t(0);
    std::unordered_map<const Node*, MathEntry> m_math;
    std::vector<DrawOp> m_ops;
    std::vector<Hit> m_hits;
    std::vector<ElementBox> m_boxes;
    std::vector<const Node*> m_hover;
    std::vector<const Node*> m_active;
    const Node* m_pressed = nullptr;
    const Node* m_focused = nullptr;
    const Node* m_focus = nullptr;
    std::string m_edit;
    std::string m_focus_start;
    bool m_invalid = false;
    const Node* m_dragging = nullptr;
    std::string m_drag_start;
    float m_drag_track_x = 0;
    float m_drag_track_w = 0;
    std::unordered_map<const Node*, float> m_switch_pos;
    std::chrono::steady_clock::time_point m_last_frame;
    bool m_has_frame = false;
    float m_dt = 0.0f;
    float m_view_w = 0.0f;
    float m_view_h = 0.0f;
    float m_scroll = 0.0f;
    float m_content_height = 0.0f;
    float m_mouse_x = 0.0f;
    float m_mouse_y = 0.0f;
    unsigned m_mods = 0;
    std::string m_cursor;
    std::unordered_map<const Node*, double> m_opacity;
    std::chrono::steady_clock::time_point m_last_activity = std::chrono::steady_clock::now();

public:
    bool idle() {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [node, pos] : m_switch_pos) {
            const float target = input_bool(*node).value_or(false) ? 1.0f : 0.0f;
            if (pos != target) m_last_activity = now;
        }
        if (m_dragging || m_pressed) m_last_activity = now;
        return std::chrono::duration<double>(now - m_last_activity).count() > 0.5;
    }

    void poke() { m_last_activity = std::chrono::steady_clock::now(); }
};

const Node* pick_window(const Project& project, const RunOptions& options) {
    if (!options.window_id.empty()) {
        const Node* n = project.document.find_by_id(options.window_id);
        return n && (n->tag() == "window" || n->tag() == "body") ? n : nullptr;
    }
    if (const Node* b = project.document.first_by_tag("body")) return b;
    if (const Node* w = project.document.first_by_tag("window")) return w;
    return &project.document.root();
}

std::optional<unsigned> parse_pixels(const std::string& text) {
    unsigned value = 0;
    std::size_t i = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        value = value * 10 + static_cast<unsigned>(text[i] - '0');
        if (value > 100000) return std::nullopt;
    }
    if (i == 0) return std::nullopt;
    const std::string rest = text.substr(i);
    if (!rest.empty() && rest != "px") return std::nullopt;
    return value;
}

struct WindowHost {
    Node* element = nullptr;
    bool main = false;
    bool popup = false;
    std::unique_ptr<bark::StyleEngine> own_styles;
    std::unique_ptr<fizmo::windows::Window> window;
    std::unique_ptr<fizmo::graphics::Renderer> renderer;
    std::unique_ptr<DocumentView> view;
    Color background;
    unsigned width = 0;
    unsigned height = 0;
    bool auto_height = false;
    bool fitted = false;
    bool close_requested = false;
    bool blocked = false;
    std::string size_attr;
    int x = 0;
    int y = 0;
    bool placed = false;
};

class WindowManager {
public:
    WindowManager(Project& project, const RunOptions& options, Node& main) : m_project(project), m_doc(project.document), m_options(options), m_main(main) {
        const fizmo::windows::MonitorInfo monitor = fizmo::windows::primary_monitor();
        if (monitor.bounds.width > 0 && monitor.bounds.height > 0) {
            m_screen = bark::Viewport{ static_cast<double>(monitor.bounds.width), static_cast<double>(monitor.bounds.height) };
        }
        DocumentHost& host = m_doc.host();
        host.main_window = &m_main;
        host.computed_style = [this](const Node& n, const std::string& property) {
            WindowHost* h = host_containing(n);
            return h ? h->view->computed(n, property) : std::string();
        };
        host.focus = [this](Node* n) {
            WindowHost* h = n ? host_containing(*n) : host_for(&m_main);
            return h && h->view->focus_from_script(n);
        };
        host.window_size = [this](const Node& n, double& w, double& h) {
            WindowHost* wh = host_for(&n);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            w = wh->window->width();
            h = wh->window->height();
            return true;
        };
        host.resize_window = [this](Node& n, double w, double h) {
            WindowHost* wh = host_for(&n);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            wh->auto_height = false;
            return wh->window->set_size(static_cast<unsigned>(w), static_cast<unsigned>(h));
        };
        host.move_window = [this](Node& n, double x, double y) {
            WindowHost* wh = host_for(&n);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            return wh->window->set_position(static_cast<int>(x), static_cast<int>(y));
        };
        host.focus_window = [this](Node& n) {
            WindowHost* wh = host_for(&n);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            wh->window->focus();
            return true;
        };
    }

    ~WindowManager() {
        while (!m_hosts.empty()) destroy_host(m_hosts.back().get());
        DocumentHost& host = m_doc.host();
        host.computed_style = nullptr;
        host.focus = nullptr;
        host.window_size = nullptr;
        host.resize_window = nullptr;
        host.move_window = nullptr;
        host.focus_window = nullptr;
        host.main_window = nullptr;
    }

    bool open_main() { return open_host(m_main, true) != nullptr; }

    int loop() {
        const double fps = m_options.max_fps > 0 ? m_options.max_fps : 60.0;
        using Clock = std::chrono::steady_clock;
        Clock::time_point next = Clock::now();
        bool quit = false;
        while (!quit) {
            std::vector<WindowHost*> snapshot;
            for (const auto& h : m_hosts) snapshot.push_back(h.get());
            for (WindowHost* h : snapshot) {
                if (h->window && h->window->is_open()) h->window->poll_events();
            }
            if (!process_close_requests()) break;
            m_doc.events().run_due_timers();
            if (m_doc.events().quit_requested()) break;
            reconcile();
            if (!host_for(&m_main)) break;
            render();
            if (m_doc.events().quit_requested()) break;

            bool idle = true;
            for (const auto& h : m_hosts) idle = idle && h->view->idle();
            const double rate = idle ? std::min(12.0, fps) : fps;
            next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate));
            const Clock::time_point now = Clock::now();
            if (next < now) next = now;
            else std::this_thread::sleep_until(next);
        }
        return 0;
    }

private:
    WindowHost* host_for(const Node* element) {
        for (const auto& h : m_hosts) {
            if (h->element == element) return h.get();
        }
        return nullptr;
    }

    WindowHost* host_containing(const Node& node) {
        Node* w = window_of(m_doc, const_cast<Node*>(&node));
        if (WindowHost* h = host_for(w)) return h;
        return host_for(&m_main);
    }

    bool owner_open(Node& element) {
        Node* owner = owner_window(m_doc, element);
        return owner == &m_main || (owner && host_for(owner));
    }

    std::string title_of(const Node& element, const bark::ComputedStyle& style) {
        std::string title = element.attribute_or("title", style.text("title").value_or(""));
        if (title.empty()) {
            if (const Node* t = m_doc.first_by_tag("title")) title = collapse_whitespace(t->inner_text());
        }
        return title.empty() ? std::string("Grove") : title;
    }

    void fire(Node& target, const char* type) {
        Event e;
        e.type = type;
        m_doc.events().dispatch(m_doc, &target, e);
    }

    WindowHost* open_host(Node& element, bool main) {
        auto host = std::make_unique<WindowHost>();
        host->element = &element;
        host->main = main;
        host->popup = is_popup(element);
        bark::StyleEngine* styles = &m_project.styles;
        if (!main) {
            host->own_styles = std::make_unique<bark::StyleEngine>(m_project.styles);
            styles = host->own_styles.get();
        }
        styles->set_viewport(m_screen);
        const bark::ComputedStyle& style = styles->style_of(element, m_project.diagnostics);
        unsigned width = static_cast<unsigned>(style.length("size", 0).value_or(main ? 800.0 : 480.0));
        std::optional<double> size_h = style.length("size", 1);
        unsigned height = static_cast<unsigned>(size_h.value_or(main ? 600.0 : 320.0));
        bool has_height = size_h.has_value();
        for (const char* name : { "width", "height" }) {
            const std::optional<std::string> text = element.attribute(name);
            if (!text) continue;
            const std::optional<unsigned> px = parse_pixels(*text);
            if (!px) {
                std::cerr << m_project.path << ": warning: <" << element.tag() << " " << name << "=\"" << *text << "\"> is not a pixel size; ignored\n";
                continue;
            }
            if (name[0] == 'w') width = *px;
            else {
                height = *px;
                has_height = true;
            }
        }
        host->width = std::max(width, 60u);
        host->height = std::max(height, 40u);
        host->auto_height = !main && !has_height;
        if (host->auto_height) host->height = host->popup ? 160u : 240u;
        host->background = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 36, 255 }));
        const std::string title = title_of(element, style);

        host->window = std::make_unique<fizmo::windows::Window>(host->width, host->height, title, host->background);
        if (!host->window->create()) {
            std::cerr << "grove: couldn't open a window for <" << element.tag() << (element.id().empty() ? "" : " id=\"" + element.id() + "\"") << ">\n";
            return nullptr;
        }
        host->renderer = std::make_unique<fizmo::graphics::Renderer>(*host->window);
        if (!host->renderer->bind()) {
            std::cerr << "grove: couldn't draw into a window\n";
            host->window->close();
            return nullptr;
        }
        if (host->popup) host->window->set_resizable(false);
        if (!main) place(*host);

        host->view = std::make_unique<DocumentView>(m_project, *styles, element, main);
        WindowHost* raw = host.get();
        host->view->set_window(host->window.get());
        host->view->set_title_callback([raw](const std::string& t) {
            if (raw->window && raw->window->is_open()) raw->window->set_title(t);
        });
        host->view->set_blocked([raw] { return raw->blocked; }, [this, raw] { focus_popup_of(*raw); });
        install_listeners(*host);
        host->size_attr = element.attribute_or("width", "") + "x" + element.attribute_or("height", "");
        m_hosts.push_back(std::move(host));
        if (!main) fire(element, "open");
        if (host_for(&element) == raw) raw->view->apply_autofocus();
        return raw;
    }

    void place(WindowHost& host) {
        WindowHost* owner = host_for(owner_window(m_doc, *host.element));
        if (!owner || !owner->window) return;
        int ox = 0;
        int oy = 0;
        if (!owner->window->position(ox, oy)) return;
        const int ow = static_cast<int>(owner->window->width());
        const int oh = static_cast<int>(owner->window->height());
        int x = ox + 48;
        int y = oy + 48;
        if (host.popup) {
            x = ox + (ow - static_cast<int>(host.width)) / 2;
            y = oy + std::max(24, (oh - static_cast<int>(host.height)) / 3);
        }
        host.window->set_position(std::max(0, x), std::max(0, y));
    }

    void install_listeners(WindowHost& host) {
        using fizmo::windows::WindowEventType;
        WindowHost* raw = &host;
        for (const WindowEventType type : { WindowEventType::MouseClick, WindowEventType::MouseRelease, WindowEventType::MouseMove, WindowEventType::MouseDoubleClick, WindowEventType::MouseScroll, WindowEventType::KeyPress, WindowEventType::KeyRelease, WindowEventType::TextInput }) {
            host.window->add_event_listener(type, [raw](const fizmo::windows::WindowEvent& e) { raw->view->on_event(e); });
        }
        host.window->add_event_listener(WindowEventType::WindowClose, [raw](const fizmo::windows::WindowEvent&) {
            raw->close_requested = true;
            if (raw->window) raw->placed = raw->window->position(raw->x, raw->y);
        });
        host.window->add_event_listener(WindowEventType::WindowFocus, [this, raw](const fizmo::windows::WindowEvent&) {
            if (raw->blocked) focus_popup_of(*raw);
            fire(*raw->element, "window_focus");
        });
        host.window->add_event_listener(WindowEventType::WindowBlur, [this, raw](const fizmo::windows::WindowEvent&) {
            fire(*raw->element, "window_blur");
        });
    }

    void focus_popup_of(WindowHost& owner) {
        for (Node* p : open_popups(m_doc, *owner.element)) {
            WindowHost* h = host_for(p);
            if (h && h->window && h->window->is_open()) {
                h->window->focus();
                return;
            }
        }
    }

    void destroy_host(WindowHost* host) {
        if (!host) return;
        if (host->view) host->view->release_states();
        host->renderer.reset();
        if (host->window && host->window->is_open()) host->window->close();
        host->window.reset();
        for (auto it = m_hosts.begin(); it != m_hosts.end(); ++it) {
            if (it->get() == host) {
                m_hosts.erase(it);
                break;
            }
        }
    }

    bool process_close_requests() {
        std::vector<Node*> requested;
        for (const auto& h : m_hosts) {
            if (h->close_requested) requested.push_back(h->element);
        }
        for (Node* element : requested) {
            WindowHost* h = host_for(element);
            if (!h) continue;
            h->close_requested = false;
            const bool main = h->main;
            const int x = h->x;
            const int y = h->y;
            const bool placed = h->placed;
            if (h->popup) {
                destroy_host(h);
                if (m_doc.is_attached(*element)) answer_popup(m_doc, *element, false);
                continue;
            }
            Event e;
            e.type = "close";
            const bool allowed = m_doc.events().dispatch(m_doc, element, e);
            destroy_host(h);
            if (!allowed) {
                WindowHost* again = open_host(*element, main);
                if (again && placed) again->window->set_position(x, y);
                if (!again && main) return false;
                continue;
            }
            if (main) return false;
            if (element->remove_attribute("open")) m_doc.touch();
        }
        return true;
    }

    void reconcile() {
        if (m_doc.revision() == m_seen_revision) return;
        for (int pass = 0; pass < 4 && m_doc.revision() != m_seen_revision; ++pass) {
            m_seen_revision = m_doc.revision();
            std::vector<Node*> closing;
            for (const auto& h : m_hosts) {
                if (h->main) continue;
                Node& el = *h->element;
                const bool wants = m_doc.is_attached(el) && el.has_attribute("open") && owner_open(el);
                if (!wants) closing.push_back(&el);
            }
            for (Node* el : closing) {
                WindowHost* h = host_for(el);
                if (!h) continue;
                const bool popup = h->popup;
                destroy_host(h);
                if (popup) {
                    if (m_doc.is_attached(*el) && !el->has_attribute("answered")) answer_popup(m_doc, *el, false);
                } else {
                    fire(*el, "close");
                }
            }
            for (Node* el : m_doc.elements_by_tag("window")) {
                if (el == &m_main || !el->has_attribute("open") || host_for(el)) continue;
                if (!owner_open(*el)) continue;
                open_host(*el, false);
            }
        }
        for (const auto& h : m_hosts) {
            h->blocked = !open_popups(m_doc, *h->element).empty();
            if (h->main || !h->window) continue;
            const std::string attr = h->element->attribute_or("width", "") + "x" + h->element->attribute_or("height", "");
            if (attr == h->size_attr) continue;
            h->size_attr = attr;
            const std::optional<unsigned> w = parse_pixels(h->element->attribute_or("width", ""));
            const std::optional<unsigned> hh = parse_pixels(h->element->attribute_or("height", ""));
            if (w || hh) {
                if (hh) h->auto_height = false;
                h->window->set_size(w.value_or(h->window->width()), hh.value_or(h->window->height()));
            }
        }
    }

    void render() {
        std::vector<WindowHost*> snapshot;
        for (const auto& h : m_hosts) snapshot.push_back(h.get());
        for (WindowHost* h : snapshot) {
            if (!host_alive(h) || !h->window || !h->window->is_open()) continue;
            fizmo::graphics::Renderer& r = *h->renderer;
            r.begin_frame();
            r.clear(h->background);
            h->view->paint(r);
            r.present();
            if (!host_alive(h)) continue;
            if (h->auto_height && !h->fitted && h->view->content_height() > 0.0f) {
                const double limit = m_screen.height > 0 ? m_screen.height * 0.85 : 900.0;
                const unsigned target = static_cast<unsigned>(std::clamp(static_cast<double>(std::ceil(h->view->content_height())), 60.0, limit));
                h->fitted = true;
                if (target != h->window->height()) {
                    int x = 0;
                    int y = 0;
                    const bool known = h->popup && h->window->position(x, y);
                    h->window->set_size(h->window->width(), target);
                    if (known) {
                        WindowHost* owner = host_for(owner_window(m_doc, *h->element));
                        int ox = 0;
                        int oy = 0;
                        if (owner && owner->window && owner->window->position(ox, oy)) {
                            const int oh = static_cast<int>(owner->window->height());
                            h->window->set_position(x, oy + std::max(24, (oh - static_cast<int>(target)) / 3));
                        }
                    }
                }
            }
        }
    }

    bool host_alive(const WindowHost* host) const {
        for (const auto& h : m_hosts) {
            if (h.get() == host) return true;
        }
        return false;
    }

    Project& m_project;
    Document& m_doc;
    const RunOptions& m_options;
    Node& m_main;
    std::vector<std::unique_ptr<WindowHost>> m_hosts;
    std::uint64_t m_seen_revision = ~std::uint64_t(0);
    bark::Viewport m_screen{ 1920, 1080 };
};

} // namespace

int run(Project& project, const RunOptions& options) {
    const Node* window = pick_window(project, options);
    if (!window) {
        std::cerr << "grove: no <window> or <body> with id \"" << options.window_id << "\" in " << project.path << '\n';
        return 1;
    }

    ScriptBuild build;
    const bool want_scripts = options.scripts && !project.scripts.empty();
    if (want_scripts && options.prebuilt) {
        build = *options.prebuilt;
    } else if (want_scripts) {
        ScriptTools tools = options.tools;
        if (tools.include_dir.empty() || tools.cache_dir.empty()) {
            const ScriptTools defaults = default_script_tools(project.path);
            if (tools.include_dir.empty()) tools.include_dir = defaults.include_dir;
            if (tools.cache_dir.empty()) tools.cache_dir = defaults.cache_dir;
            if (tools.walnut.empty()) tools.walnut = defaults.walnut;
            if (tools.cxx.empty()) tools.cxx = defaults.cxx;
            if (tools.cc.empty()) tools.cc = defaults.cc;
        }
        const std::size_t errors_before = project.diagnostics.error_count();
        const auto t0 = std::chrono::steady_clock::now();
        build = build_scripts(project, tools, &std::cerr);
        std::size_t compiled = 0;
        for (const BuiltScript& b : build.scripts) compiled += (b.runnable && b.ok && !b.cached) ? 1 : 0;
        if (compiled) {
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cerr << "grove: built " << compiled << " script" << (compiled == 1 ? "" : "s") << " in " << std::lround(secs * 10) / 10.0 << "s\n";
        }
        const auto& items = project.diagnostics.items();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i < items.size() - (project.diagnostics.error_count() - errors_before)) continue;
            const Diagnostic& d = items[i];
            std::cerr << d.file << ':' << d.span.begin.line << ':' << d.span.begin.column << ": " << to_string(d.severity) << ": " << d.message << '\n';
        }
    }

    Document& doc = project.document;
    grove_bind_document(reinterpret_cast<grove_document*>(&doc));
    doc.events().set_error_handler([](const std::string& message) { std::cerr << "grove: script error: " << message << '\n'; });
    int code = 0;
    {
        WindowManager windows(project, options, const_cast<Node&>(*window));
        if (!windows.open_main()) {
            std::cerr << "grove: couldn't open a window\n";
            grove_bind_document(nullptr);
            return 1;
        }
        if (want_scripts) {
            const std::size_t errors_before = project.diagnostics.error_count();
            run_scripts(doc, build, project.diagnostics, project.path);
            const auto& items = project.diagnostics.items();
            const std::size_t added = project.diagnostics.error_count() - errors_before;
            for (std::size_t i = items.size() - std::min(items.size(), added); i < items.size(); ++i) {
                const Diagnostic& d = items[i];
                std::cerr << d.file << ':' << d.span.begin.line << ':' << d.span.begin.column << ": " << to_string(d.severity) << ": " << d.message << '\n';
            }
        }
        {
            Event load;
            load.type = "load";
            load.bubbles = false;
            doc.events().dispatch(doc, nullptr, load);
        }
        code = windows.loop();
    }
    doc.events().clear();
    grove_bind_document(nullptr);
    return code;
}

} // namespace runtime
} // namespace grove
