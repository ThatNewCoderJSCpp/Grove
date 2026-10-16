#include "grove/runtime/runtime.hpp"
#include "grove/markup/tags.hpp"
#include "grove/api/grove_api.h"

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
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
    auto at = [&](std::size_t i) { return static_cast<float>(bark::as_length(*v, fs, i).value_or(0.0)); };
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

struct DrawOp {
    enum class Kind { Fill, Stroke, Text, Math } kind = Kind::Fill;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    Color color;
    unsigned stroke = 1;
    std::string text;
    fizmo::text::TextStyle style;
    const fizmo::text::FancyText* math = nullptr;
};

class DocumentView {
public:
    DocumentView(Project& project, const Node& window) : m_project(project), m_window(window) {}

    void paint(fizmo::graphics::Renderer& r) {
        if (m_revision != m_project.document.revision()) {
            m_revision = m_project.document.revision();
            m_project.styles.invalidate();
            m_math.clear();
        }
        m_ops.clear();
        const bark::ComputedStyle& style = style_of(m_window);
        const float w = static_cast<float>(r.viewport_width());
        const float h = static_cast<float>(r.viewport_height());
        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, 0, 0, w, h, to_color(*bg));
        const Box pad = box_of(style, "padding");
        float y = pad.top;
        layout_children(r, m_window, pad.left, y, w - pad.left - pad.right);
        for (const DrawOp& op : m_ops) draw(r, op);
    }

private:
    const bark::ComputedStyle& style_of(const Node& node) { return m_project.styles.style_of(node, m_project.diagnostics); }

    std::size_t push_rect(DrawOp::Kind kind, float x, float y, float w, float h, const Color& color, unsigned stroke = 1) {
        DrawOp op;
        op.kind = kind;
        op.x = x;
        op.y = y;
        op.w = w;
        op.h = h;
        op.color = color;
        op.stroke = stroke;
        m_ops.push_back(std::move(op));
        return m_ops.size() - 1;
    }

    static void draw(fizmo::graphics::Renderer& r, const DrawOp& op) {
        const int x = static_cast<int>(std::lround(op.x));
        const int y = static_cast<int>(std::lround(op.y));
        const unsigned w = static_cast<unsigned>(std::max(0.0f, op.w));
        const unsigned h = static_cast<unsigned>(std::max(0.0f, op.h));
        switch (op.kind) {
            case DrawOp::Kind::Fill: r.draw_rect(x, y, w, h, fizmo::graphics::Paint::fill(op.color)); break;
            case DrawOp::Kind::Stroke: r.draw_rect(x, y, w, h, fizmo::graphics::Paint::stroke(op.color, op.stroke)); break;
            case DrawOp::Kind::Text: r.draw_text(x, y, std::max(w, 1u), std::max(h, 1u), op.text, op.style); break;
            case DrawOp::Kind::Math: if (op.math) r.draw_fancy_text(*op.math, op.x, op.y); break;
        }
    }

    double font_size_of(const Node& element) { return style_of(element).font_size(); }

    fizmo::text::TextStyle text_style(const Node& element) {
        const bark::ComputedStyle& style = style_of(element);
        const Color color = to_color(style.color("color").value_or(bark::Rgba{ 230, 230, 230, 255 }));
        fizmo::text::TextStyle ts(font_size_of(element), color);
        if (style.text("font-weight").value_or("") == "bold") ts.set_bold(true);
        return ts;
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
        DrawOp op;
        op.kind = DrawOp::Kind::Text;
        op.style = text_style(owner);
        const unsigned w = static_cast<unsigned>(std::max(1.0f, width));
        const fizmo::text::TextMetrics m = r.measure_text(text, op.style, w);
        op.x = x;
        op.y = y;
        op.w = static_cast<float>(w);
        op.h = static_cast<float>(std::max(m.height, 1u));
        op.text = text;
        y += op.h;
        m_ops.push_back(std::move(op));
    }

    void layout_math(const Node& element, float x, float& y, float width) {
        auto it = m_math.find(&element);
        if (it == m_math.end()) {
            const bark::ComputedStyle& style = style_of(element);
            const Color color = to_color(style.color("math-color").value_or(style.color("color").value_or(bark::Rgba{ 230, 230, 230, 255 })));
            fizmo::text::MathRenderOptions options = fizmo::text::detail::simple_math_options(static_cast<float>(font_size_of(element)), color);
            options.brace_syntax = math_syntax_of(element, m_project.path, m_project.diagnostics) == MathSyntax::Brace;
            options.layout.display = element.attribute_or("display", "inline") == "block";
            it = m_math.emplace(&element, fizmo::text::make_fancy_math(math_source_of(element), options)).first;
        }
        const fizmo::text::FancyText& ft = it->second;
        float dx = x;
        const std::string align = style_of(element).text("text-align").value_or("left");
        if (align == "center") dx = x + std::max(0.0f, (width - ft.width()) / 2.0f);
        else if (align == "right") dx = x + std::max(0.0f, width - ft.width());
        DrawOp op;
        op.kind = DrawOp::Kind::Math;
        op.x = dx;
        op.y = y;
        op.math = &ft;
        m_ops.push_back(std::move(op));
        y += ft.height();
    }

    void layout_element(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        if (style.text("display").value_or("") == "none") return;

        const Box margin = box_of(style, "margin");
        const Box pad = box_of(style, "padding");
        y += margin.top;
        const float bx = x + margin.left;
        const float bw = width - margin.left - margin.right;
        const float content_x = bx + pad.left;
        const float content_w = bw - pad.left - pad.right;
        const float top = y;

        const auto bg = style.color("background");
        const auto border = style.color("border-color");
        const std::size_t bg_op = bg ? push_rect(DrawOp::Kind::Fill, bx, top, bw, 0, to_color(*bg)) : std::size_t(-1);

        float cy = y + pad.top;
        if (element.tag() == "math") layout_math(element, content_x, cy, content_w);
        else if (element.tag() == "br") cy += static_cast<float>(font_size_of(element));
        else if (element.tag() == "hr") {
            push_rect(DrawOp::Kind::Fill, content_x, cy + 4, content_w, 1, to_color(style.color("color").value_or(bark::Rgba{ 120, 120, 120, 255 })));
            cy += 9;
        } else layout_children(r, element, content_x, cy, content_w);

        const float bottom = cy + pad.bottom;
        if (bg) m_ops[bg_op].h = bottom - top;
        if (border) push_rect(DrawOp::Kind::Stroke, bx, top, bw, bottom - top, to_color(*border), static_cast<unsigned>(style.length("border-width").value_or(1.0)));
        y = bottom + margin.bottom;
        if (auto gap = style.length("gap")) y += static_cast<float>(*gap);
    }

    Project& m_project;
    const Node& m_window;
    std::uint64_t m_revision = ~std::uint64_t(0);
    std::unordered_map<const Node*, fizmo::text::FancyText> m_math;
    std::vector<DrawOp> m_ops;
};

const Node* pick_window(const Project& project, const RunOptions& options) {
    if (!options.window_id.empty()) {
        const Node* n = project.document.find_by_id(options.window_id);
        return n && n->tag() == "window" ? n : nullptr;
    }
    if (const Node* w = project.document.first_by_tag("window")) return w;
    if (const Node* b = project.document.first_by_tag("body")) return b;
    return &project.document.root();
}

} // namespace

int run(Project& project, const RunOptions& options) {
    const Node* window = pick_window(project, options);
    if (!window) {
        std::cerr << "grove: no <window id=\"" << options.window_id << "\"> in " << project.path << '\n';
        return 1;
    }
    const bark::ComputedStyle& style = project.styles.style_of(*window, project.diagnostics);

    std::string title = window->attribute_or("title", style.text("title").value_or(""));
    if (title.empty()) {
        if (const Node* t = project.document.first_by_tag("title")) title = collapse_whitespace(t->inner_text());
    }
    if (title.empty()) title = "Grove";

    unsigned width = static_cast<unsigned>(style.length("size", 0).value_or(800.0));
    unsigned height = static_cast<unsigned>(style.length("size", 1).value_or(600.0));
    if (auto w = window->attribute("width")) width = static_cast<unsigned>(std::stoul(*w));
    if (auto h = window->attribute("height")) height = static_cast<unsigned>(std::stoul(*h));

    const Color background = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 36, 255 }));

    fizmo::windows::Application app(width, height, title, background);
    app.set_clear_color(background);
    if (options.max_fps > 0) app.set_max_fps(options.max_fps);

    grove_bind_document(reinterpret_cast<grove_document*>(&project.document));
    DocumentView view(project, *window);
    app.on_render([&](fizmo::graphics::Renderer& r) { view.paint(r); });
    const int code = app.run();
    grove_bind_document(nullptr);
    return code;
}

} // namespace runtime
} // namespace grove
