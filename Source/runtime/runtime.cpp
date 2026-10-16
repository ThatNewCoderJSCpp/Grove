#include "grove/runtime/runtime.hpp"
#include "grove/markup/tags.hpp"
#include "grove/api/grove_api.h"
#include "grove/input/input.hpp"

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
#include <iostream>
#include <optional>
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

class DocumentView {
public:
    DocumentView(Project& project, const Node& window) : m_project(project), m_window(window) {}

    void set_window(fizmo::windows::Window* window) noexcept { m_native = window; }

    void paint(fizmo::graphics::Renderer& r) {
        if (m_revision != m_project.document.revision()) {
            m_revision = m_project.document.revision();
            m_project.styles.invalidate();
        }
        const auto now = std::chrono::steady_clock::now();
        m_dt = m_has_frame ? std::min(0.1f, std::chrono::duration<float>(now - m_last_frame).count()) : 0.0f;
        m_last_frame = now;
        m_has_frame = true;
        const float w = static_cast<float>(r.viewport_width());
        const float h = static_cast<float>(r.viewport_height());
        m_project.styles.set_viewport(bark::Viewport{ w, h });
        m_ops.clear();
        m_hits.clear();
        const bark::ComputedStyle& style = style_of(m_window);
        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, 0, 0, w, h, to_color(*bg));
        const Box pad = box_of(style, "padding");
        float y = pad.top;
        layout_children(r, m_window, pad.left, y, w - pad.left - pad.right);
        for (const DrawOp& op : m_ops) draw(r, op);
    }

    void on_event(const fizmo::windows::WindowEvent& e) {
        using fizmo::windows::WindowEventType;
        switch (e.type) {
            case WindowEventType::MouseClick: if (e.button == 1) mouse_down(static_cast<float>(e.x), static_cast<float>(e.y)); break;
            case WindowEventType::MouseRelease: if (e.button == 1) m_dragging = nullptr; break;
            case WindowEventType::MouseMove: if (m_dragging) drag_to(static_cast<float>(e.x)); break;
            case WindowEventType::TextInput: type_text(e.text); break;
            case WindowEventType::KeyPress: key_down(e); break;
            default: break;
        }
    }

private:
    const bark::ComputedStyle& style_of(const Node& node) { return m_project.styles.style_of(node, m_project.diagnostics); }
    Node& mutable_node(const Node& node) { return const_cast<Node&>(node); }

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

    Color text_color(const Node& element) { return to_color(style_of(element).color("color").value_or(bark::Rgba{ 230, 230, 230, 255 })); }
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

    void layout_children(fizmo::graphics::Renderer& r, const Node& parent, float x, float& y, float width) {
        for (const auto& child : parent.children()) {
            if (child->is_text()) layout_text(r, parent, child->text(), x, y, width);
            else if (child->is_element()) layout_element(r, *child, x, y, width);
        }
    }

    void layout_text(fizmo::graphics::Renderer& r, const Node& owner, const std::string& raw, float x, float& y, float width) {
        const std::string text = collapse_whitespace(raw);
        if (text.empty()) return;
        const fizmo::text::TextStyle style = text_style(owner);
        const unsigned w = static_cast<unsigned>(std::max(1.0f, width));
        const fizmo::text::TextMetrics m = r.measure_text(text, style, w);
        const float h = static_cast<float>(std::max(m.height, 1u));
        push_text(x, y, static_cast<float>(w), h, text, style);
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
        const double fade = disabled ? 0.45 : 1.0;

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
            if (disabled) shown.set_fg_color(with_alpha(text_color(element), fade));
            push_text(tx, y + pad.top, tw, lh, text, shown);
        }
        if (focused) {
            const float cx = std::min(tx + text_width(r, text, ts) + 1, x + w - pad.right);
            push_rect(DrawOp::Kind::Fill, cx, y + pad.top + 2, 2, lh - 4, accent_color(element));
        }
        if (!disabled) m_hits.push_back(Hit{ &element, kind, x, y, w, h, 0, 0 });
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
        const double fade = disabled ? 0.45 : 1.0;
        const Color accent = with_alpha(accent_color(element), fade);
        const Color outline = with_alpha(text_color(element), 0.55 * fade);
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
            const Color off_track = with_alpha(text_color(element), 0.22 * fade);
            push_rect(DrawOp::Kind::Fill, x, by, box, size, mix(off_track, accent, pos), 1, size / 2.0f);
            const float knob = size / 2.0f - 3.0f;
            const float kx = x + size / 2.0f + pos * (box - size);
            push(DrawOp::Kind::Circle, kx, by + size / 2.0f, 0, 0, mix(with_alpha(text_color(element), fade), background, pos)).radius = knob;
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
            if (disabled) shown.set_fg_color(with_alpha(text_color(element), fade));
            push_text(lx, y + (row - lh) / 2.0f, lw, lh, label, shown);
            w = std::min(width, box + 8 + text_width(r, label, ts));
        }
        if (!disabled) m_hits.push_back(Hit{ &element, kind, x, y, w, row, 0, 0 });
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
        const double fade = disabled ? 0.45 : 1.0;
        const Color accent = with_alpha(accent_color(element), fade);

        push_rect(DrawOp::Kind::Fill, track_x, cy - 2, track_w, 4, with_alpha(text_color(element), 0.2 * fade), 1, 2);
        push_rect(DrawOp::Kind::Fill, track_x, cy - 2, vx - track_x, 4, accent, 1, 2);
        push(DrawOp::Kind::Circle, vx, cy, 0, 0, accent).radius = thumb;

        const std::string shown = input_text(element);
        const float avail = width - w - 10;
        if (avail > 20) push_text(x + w + 10, y + (row - lh) / 2.0f, avail, lh, shown, ts);
        if (!disabled) m_hits.push_back(Hit{ &element, InputKind::Slider, x, y, w, row, track_x, track_w });
        y += row;
    }

    void layout_element(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        if (style.text("display").value_or("") == "none") return;

        const Box margin = box_of(style, "margin");
        y += margin.top;
        const float bx = x + margin.left;
        const float bw = width - margin.left - margin.right;

        if (const std::optional<InputKind> kind = input_kind_of(element)) {
            if (is_text_input(*kind)) layout_text_input(r, element, *kind, bx, y, bw);
            else if (*kind == InputKind::Slider) layout_slider(r, element, bx, y, bw);
            else layout_check(r, element, *kind, bx, y, bw);
            y += margin.bottom;
            if (auto gap = style.length("gap")) y += static_cast<float>(*gap);
            return;
        }

        const Box pad = box_of(style, "padding");
        const float content_x = bx + pad.left;
        const float content_w = bw - pad.left - pad.right;
        const float top = y;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));

        const auto bg = style.color("background");
        const auto border = style.color("border-color");
        const std::size_t bg_op = bg ? push_rect(DrawOp::Kind::Fill, bx, top, bw, 0, to_color(*bg), 1, radius) : std::size_t(-1);

        float cy = y + pad.top;
        if (element.tag() == "math") layout_math(element, content_x, cy, content_w);
        else if (element.tag() == "br") cy += static_cast<float>(font_size_of(element));
        else if (element.tag() == "hr") {
            push_rect(DrawOp::Kind::Fill, content_x, cy + 4, content_w, 1, to_color(style.color("color").value_or(bark::Rgba{ 120, 120, 120, 255 })));
            cy += 9;
        } else layout_children(r, element, content_x, cy, content_w);

        const float bottom = cy + pad.bottom;
        if (bg) m_ops[bg_op].h = bottom - top;
        if (border) push_rect(DrawOp::Kind::Stroke, bx, top, bw, bottom - top, to_color(*border), static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);
        y = bottom + margin.bottom;
        if (auto gap = style.length("gap")) y += static_cast<float>(*gap);
    }

    void focus(const Node* node) {
        if (m_focus == node) return;
        blur(true);
        m_focus = node;
        m_edit = input_text(*node);
        m_invalid = false;
        if (m_native) m_native->start_text_input();
    }

    void blur(bool commit) {
        if (!m_focus) return;
        if (commit) commit_input_text(m_project.document, mutable_node(*m_focus), m_edit);
        m_focus = nullptr;
        m_edit.clear();
        m_invalid = false;
        if (m_native) m_native->stop_text_input();
    }

    void mouse_down(float px, float py) {
        for (const Hit& hit : m_hits) {
            if (!hit.contains(px, py)) continue;
            Node& node = mutable_node(*hit.node);
            switch (hit.kind) {
                case InputKind::Bool:
                    blur(true);
                    set_input_bool(m_project.document, node, !input_bool(node).value_or(false));
                    return;
                case InputKind::Radio:
                    blur(true);
                    set_input_bool(m_project.document, node, true);
                    return;
                case InputKind::Slider:
                    blur(true);
                    m_dragging = hit.node;
                    m_drag_track_x = hit.track_x;
                    m_drag_track_w = hit.track_w;
                    drag_to(px);
                    return;
                default:
                    focus(hit.node);
                    return;
            }
        }
        blur(true);
    }

    void drag_to(float px) {
        if (!m_dragging) return;
        const InputRange range = input_range(*m_dragging);
        const double t = std::clamp((px - m_drag_track_x) / std::max(1.0f, m_drag_track_w), 0.0f, 1.0f);
        set_input_float(m_project.document, mutable_node(*m_dragging), range.min + t * (range.max - range.min));
    }

    void type_text(const std::string& text) {
        if (!m_focus) return;
        for (const std::string& cp : split_code_points(text)) {
            if (input_accepts(*m_focus, m_edit, cp)) {
                m_edit += cp;
                m_invalid = false;
            }
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
        if (m_focus && !commit_input_text(m_project.document, mutable_node(*m_focus), m_edit)) m_edit = input_text(*m_focus);
        const Node* next = order[index];
        m_focus = nullptr;
        focus(next);
    }

    void key_down(const fizmo::windows::WindowEvent& e) {
        using fizmo::input::Key;
        if (!m_focus) return;
        const Key key = e.logical_key != Key::Unknown ? e.logical_key : e.physical_key;
        Node& node = mutable_node(*m_focus);
        switch (key) {
            case Key::Backspace:
                pop_code_point(m_edit);
                m_invalid = false;
                break;
            case Key::Enter:
                if (commit_input_text(m_project.document, node, m_edit)) blur(false);
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
                if (commit_input_text(m_project.document, node, m_edit) || input_kind_of(node) != InputKind::String) {
                    if (step_input(m_project.document, node, key == Key::Up ? 1 : -1)) {
                        m_edit = input_text(node);
                        m_invalid = false;
                    }
                }
                break;
            default:
                break;
        }
    }

    Project& m_project;
    const Node& m_window;
    fizmo::windows::Window* m_native = nullptr;
    std::uint64_t m_revision = ~std::uint64_t(0);
    std::unordered_map<const Node*, MathEntry> m_math;
    std::vector<DrawOp> m_ops;
    std::vector<Hit> m_hits;
    const Node* m_focus = nullptr;
    std::string m_edit;
    bool m_invalid = false;
    const Node* m_dragging = nullptr;
    std::unordered_map<const Node*, float> m_switch_pos;
    std::chrono::steady_clock::time_point m_last_frame;
    bool m_has_frame = false;
    float m_dt = 0.0f;
    float m_drag_track_x = 0;
    float m_drag_track_w = 0;
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

} // namespace

int run(Project& project, const RunOptions& options) {
    const Node* window = pick_window(project, options);
    if (!window) {
        std::cerr << "grove: no <window> or <body> with id \"" << options.window_id << "\" in " << project.path << '\n';
        return 1;
    }
    std::string title = window->attribute_or("title", project.styles.style_of(*window, project.diagnostics).text("title").value_or(""));
    if (title.empty()) {
        if (const Node* t = project.document.first_by_tag("title")) title = collapse_whitespace(t->inner_text());
    }
    if (title.empty()) title = "Grove";

    const fizmo::windows::MonitorInfo monitor = fizmo::windows::primary_monitor();
    if (monitor.bounds.width > 0 && monitor.bounds.height > 0) {
        project.styles.set_viewport(bark::Viewport{ static_cast<double>(monitor.bounds.width), static_cast<double>(monitor.bounds.height) });
    }
    const bark::ComputedStyle& style = project.styles.style_of(*window, project.diagnostics);

    unsigned width = static_cast<unsigned>(style.length("size", 0).value_or(800.0));
    unsigned height = static_cast<unsigned>(style.length("size", 1).value_or(600.0));
    for (const char* name : { "width", "height" }) {
        const std::optional<std::string> text = window->attribute(name);
        if (!text) continue;
        const std::optional<unsigned> px = parse_pixels(*text);
        if (!px) {
            std::cerr << project.path << ": warning: <" << window->tag() << " " << name << "=\"" << *text << "\"> is not a pixel size; ignored\n";
            continue;
        }
        (name[0] == 'w' ? width : height) = *px;
    }

    const Color background = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 36, 255 }));

    fizmo::windows::Application app(width, height, title, background);
    app.set_clear_color(background);
    if (options.max_fps > 0) app.set_max_fps(options.max_fps);

    grove_bind_document(reinterpret_cast<grove_document*>(&project.document));
    DocumentView view(project, *window);
    view.set_window(&app.window());
    app.on_render([&](fizmo::graphics::Renderer& r) { view.paint(r); });
    for (const fizmo::windows::WindowEventType type : { fizmo::windows::WindowEventType::MouseClick, fizmo::windows::WindowEventType::MouseRelease, fizmo::windows::WindowEventType::MouseMove, fizmo::windows::WindowEventType::KeyPress, fizmo::windows::WindowEventType::TextInput }) {
        app.add_event_listener(type, [&view](const fizmo::windows::WindowEvent& e) { view.on_event(e); });
    }
    const int code = app.run();
    grove_bind_document(nullptr);
    return code;
}

} // namespace runtime
} // namespace grove
