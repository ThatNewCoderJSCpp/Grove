#include "grove/runtime/runtime.hpp"
#include "grove/pages.hpp"
#include "grove/markup/tags.hpp"
#include "grove/api/grove_api.h"
#include "grove/input/input.hpp"
#include "grove/input/code_buffer.hpp"
#include "grove/native.hpp"
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
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
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

struct AutoSides {
    bool top = false;
    bool right = false;
    bool bottom = false;
    bool left = false;
};

AutoSides auto_sides(const bark::ComputedStyle& style, const std::string& property) {
    AutoSides a;
    const bark::ValueList* v = style.get(property);
    if (!v || v->empty()) return a;
    auto is = [&](std::size_t i) { return (*v)[i].is_ident("auto"); };
    switch (v->size()) {
        case 1: a.top = a.right = a.bottom = a.left = is(0); break;
        case 2: a.top = a.bottom = is(0); a.left = a.right = is(1); break;
        case 3: a.top = is(0); a.left = a.right = is(1); a.bottom = is(2); break;
        default: a.top = is(0); a.right = is(1); a.bottom = is(2); a.left = is(3); break;
    }
    return a;
}

std::string align_word(const std::string& w) {
    if (w == "start" || w == "flex-start" || w == "self-start" || w == "left" || w == "top" || w == "baseline") return "start";
    if (w == "end" || w == "flex-end" || w == "self-end" || w == "right" || w == "bottom") return "end";
    if (w == "center" || w == "middle") return "center";
    if (w == "stretch" || w == "space-between" || w == "space-around" || w == "space-evenly") return w;
    return std::string();
}

struct Alignment {
    std::string main = "start";
    std::string cross = "stretch";
};

Alignment alignment_of(const bark::ComputedStyle& style, bool row) {
    Alignment al;
    std::string horizontal;
    std::string vertical;
    if (const bark::ValueList* a = style.get("align")) bark::split_align(*a, horizontal, vertical);
    const std::string main = align_word(row ? horizontal : vertical);
    const std::string cross = align_word(row ? vertical : horizontal);
    if (!main.empty()) al.main = main;
    if (!cross.empty()) al.cross = cross;
    if (const auto j = style.text("justify-content")) {
        const std::string w = align_word(*j);
        if (!w.empty()) al.main = w;
    }
    if (const auto ai = style.text("align-items")) {
        const std::string w = align_word(*ai);
        if (!w.empty()) al.cross = w;
    }
    return al;
}

void spread(const std::string& how, float free, std::vector<float>& before, std::vector<float>& after) {
    const std::size_t n = before.size();
    if (n == 0 || free <= 0.0f) return;
    const float count = static_cast<float>(n);
    if (how == "center") before[0] += free / 2.0f;
    else if (how == "end") before[0] += free;
    else if (how == "space-between") {
        if (n == 1) return;
        for (std::size_t i = 0; i + 1 < n; ++i) after[i] += free / (count - 1.0f);
    } else if (how == "space-around") {
        for (std::size_t i = 0; i < n; ++i) {
            before[i] += free / (2.0f * count);
            after[i] += free / (2.0f * count);
        }
    } else if (how == "space-evenly") {
        for (std::size_t i = 0; i < n; ++i) before[i] += free / (count + 1.0f);
    }
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

struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect intersect(const Rect& o) const noexcept {
        const float l = std::max(x, o.x);
        const float t = std::max(y, o.y);
        const float r = std::min(x + w, o.x + o.w);
        const float b = std::min(y + h, o.y + o.h);
        return Rect{ l, t, std::max(0.0f, r - l), std::max(0.0f, b - t) };
    }
};

constexpr Rect k_everywhere{ -1e7f, -1e7f, 2e7f, 2e7f };

std::optional<float> resolve_length(const bark::ComputedStyle& style, const std::string& property, float base, std::size_t index = 0) {
    const bark::ValueList* v = style.get(property);
    if (!v || index >= v->size()) return std::nullopt;
    const bark::Value& value = (*v)[index];
    if (value.is_ident("auto") || value.is_ident("none")) return std::nullopt;
    if (value.is_number() && value.unit == "%") {
        if (base < 0.0f) return std::nullopt;
        return base * static_cast<float>(value.number) / 100.0f;
    }
    if (bark::is_math_function(value)) {
        const auto v = bark::evaluate_length(value, style.font_size(), style.viewport(), base < 0.0f ? std::nullopt : std::optional<double>(base));
        if (!v) return std::nullopt;
        return static_cast<float>(*v);
    }
    const auto len = style.length(property, index);
    if (!len) return std::nullopt;
    return static_cast<float>(*len);
}

std::optional<fizmo::text::Font> font_named(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "monospace" || name == "mono" || name == "code") return fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono);
    if (name == "serif") return fizmo::text::to_font(fizmo::text::SerifFont::Georgia);
    if (name == "sans-serif" || name == "sans") return std::nullopt;
    for (std::uint16_t i = 1; i < static_cast<std::uint16_t>(fizmo::text::Font::Count_); ++i) {
        const fizmo::text::Font f = static_cast<fizmo::text::Font>(i);
        std::string css = fizmo::text::font_css_name(f);
        std::transform(css.begin(), css.end(), css.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (css == name) return f;
    }
    return std::nullopt;
}

std::vector<fizmo::windows::dialogs::FileFilter> parse_filters(const std::string& text) {
    std::vector<fizmo::windows::dialogs::FileFilter> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::string entry = text.substr(start, end - start);
        start = end + 1;
        fizmo::windows::dialogs::FileFilter f;
        const std::size_t colon = entry.find(':');
        std::string patterns = entry;
        if (colon != std::string::npos) {
            f.name = entry.substr(0, colon);
            patterns = entry.substr(colon + 1);
        }
        std::string current;
        for (char c : patterns + " ") {
            if (c == ' ' || c == ',' || c == '\t') {
                if (!current.empty()) f.patterns.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (f.patterns.empty()) continue;
        while (!f.name.empty() && f.name.front() == ' ') f.name.erase(f.name.begin());
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<std::string> show_file_dialog(const FileDialogRequest& request, void* parent) {
    fizmo::windows::dialogs::FileDialogOptions o;
    o.title = request.title;
    o.default_path = request.start;
    o.filters = parse_filters(request.filter);
    o.multiple = request.multiple;
    o.parent = parent;
    const int kind = request.mode == "save" ? 1 : request.mode == "folder" ? 2 : 0;
    try {
        return fizmo::windows::dialogs::detail::file_dialog(o, kind);
    } catch (...) {
        return {};
    }
}

FileDialogRequest dialog_request_for(Node& node) {
    FileDialogRequest r;
    r.mode = node.attribute_or("mode", "open");
    r.title = node.attribute_or("title", r.mode == "folder" ? "Choose a folder" : r.mode == "save" ? "Save as" : "Choose a file");
    r.filter = node.attribute_or("filter", "");
    r.start = node.attribute_or("start", "");
    r.multiple = node.has_attribute("multiple");
    r.owner = &node;
    const std::string current = input_string(node).value_or("");
    if (r.start.empty() && !current.empty()) r.start = current.substr(0, current.find('\n'));
    return r;
}

std::string base_name(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

struct CodeColors {
    Color keyword{ 203, 166, 247, 255 };
    Color type{ 249, 226, 175, 255 };
    Color string{ 166, 227, 161, 255 };
    Color number{ 250, 179, 135, 255 };
    Color comment{ 127, 132, 156, 255 };
    Color preprocessor{ 245, 194, 231, 255 };
    Color function{ 137, 180, 250, 255 };
    Color op{ 137, 220, 235, 255 };
    Color tag{ 137, 180, 250, 255 };
    Color attribute{ 249, 226, 175, 255 };
    Color property{ 137, 220, 235, 255 };
    Color constant{ 250, 179, 135, 255 };
    Color name_space{ 148, 226, 213, 255 };
    Color variable{ 242, 205, 205, 255 };
    Color escape{ 245, 194, 231, 255 };
};

struct CodeTokenLook {
    std::optional<Color> color;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

CodeTokenLook code_token_look(const bark::ComputedStyle& style, CodeTokenKind kind) {
    CodeTokenLook look;
    const CodeColors palette;
    switch (kind) {
        case CodeTokenKind::Keyword: look.color = palette.keyword; break;
        case CodeTokenKind::Type: look.color = palette.type; break;
        case CodeTokenKind::String: look.color = palette.string; break;
        case CodeTokenKind::Number: look.color = palette.number; break;
        case CodeTokenKind::Comment: look.color = palette.comment; look.italic = true; break;
        case CodeTokenKind::Preprocessor: look.color = palette.preprocessor; break;
        case CodeTokenKind::Function: look.color = palette.function; break;
        case CodeTokenKind::Operator: look.color = palette.op; break;
        case CodeTokenKind::Tag: look.color = palette.tag; break;
        case CodeTokenKind::Attribute: look.color = palette.attribute; break;
        case CodeTokenKind::Property: look.color = palette.property; break;
        case CodeTokenKind::Constant: look.color = palette.constant; break;
        case CodeTokenKind::Namespace: look.color = palette.name_space; break;
        case CodeTokenKind::Variable: look.color = palette.variable; break;
        case CodeTokenKind::Escape: look.color = palette.escape; break;
        case CodeTokenKind::Plain:
        case CodeTokenKind::ControlKeyword:
        case CodeTokenKind::BuiltinType:
        case CodeTokenKind::Declaration:
        case CodeTokenKind::SelfKeyword:
        case CodeTokenKind::ThisKeyword:
        case CodeTokenKind::Cast:
        case CodeTokenKind::BuiltinOperator:
        case CodeTokenKind::OverloadedOperator:
        case CodeTokenKind::EnumMember:
        case CodeTokenKind::TemplateScope:
        case CodeTokenKind::VariableTemplate:
        case CodeTokenKind::Unresolved: break;
    }
    const bark::ValueList* values = style.get(std::string("code-") + to_string(kind));
    if (!values || values->empty()) return look;
    look.bold = false;
    look.italic = false;
    look.underline = false;
    for (const bark::Value& v : *values) {
        if (v.is_ident("bold")) look.bold = true;
        else if (v.is_ident("italic")) look.italic = true;
        else if (v.is_ident("underline")) look.underline = true;
        else if (v.is_ident("normal") || v.is_ident("inherit")) continue;
        else if (auto c = bark::as_color(bark::ValueList{ v })) look.color = to_color(*c);
    }
    return look;
}

std::vector<CodeTokenLook> code_token_looks(const bark::ComputedStyle& style) {
    std::vector<CodeTokenLook> looks;
    for (std::size_t k = 0; k <= static_cast<std::size_t>(CodeTokenKind::Unresolved); ++k) {
        const CodeTokenKind kind = static_cast<CodeTokenKind>(k);
        const CodeTokenKind base = code_token_fallback(kind);
        const bark::ValueList* own = style.get(std::string("code-") + to_string(kind));
        if (base != kind && (!own || own->empty())) looks.push_back(looks[static_cast<std::size_t>(base)]);
        else looks.push_back(code_token_look(style, kind));
        if (k == 0) {
            if (auto plain = style.color("code-plain")) looks[0].color = to_color(*plain);
        }
    }
    return looks;
}

CodeTokenLook code_extra_look(const bark::ComputedStyle& style, const char* property) {
    CodeTokenLook look;
    const bark::ValueList* values = style.get(property);
    if (!values) return look;
    for (const bark::Value& v : *values) {
        if (v.is_ident("bold")) look.bold = true;
        else if (v.is_ident("italic")) look.italic = true;
        else if (v.is_ident("underline")) look.underline = true;
        else if (auto c = bark::as_color(bark::ValueList{ v })) look.color = to_color(*c);
    }
    return look;
}

std::vector<CodeTokenKind> code_marks(std::string_view text) {
    std::vector<CodeTokenKind> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ',')) ++i;
        std::size_t end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != ',') ++end;
        if (end > i) {
            if (auto kind = code_token_kind_from_name(text.substr(i, end - i))) out.push_back(*kind);
        }
        i = end;
    }
    return out;
}

struct CodeEditor {
    CodeBuffer buffer;
    std::string synced;
    std::string focus_start;
    std::vector<CodeLineState> states;
    std::size_t states_revision = static_cast<std::size_t>(-1);
    float scroll_x = 0;
    float scroll_y = 0;
    bool reveal = false;
    float text_x = 0;
    float text_y = 0;
    float char_w = 8;
    float line_h = 16;
    float view_w = 0;
    float view_h = 0;
    std::size_t tab_size = 4;
    CodePos last_reported{ static_cast<std::size_t>(-1), 0 };
    std::string caret_request;
    bool column_mode = false;
    CodePos column_anchor;
    std::size_t column_head = 0;
    CodeOverlay overlay;
    std::string overlay_spans;
    std::string overlay_problems;
    CodeOverlay links;
    std::string links_attr;
};

struct Scroller {
    const Node* node = nullptr;
    Rect rect;
    float max = 0;
    bool code = false;
};

struct PendingDialog {
    const Node* node = nullptr;
    std::future<std::vector<std::string>> result;
};

std::shared_ptr<fizmo::images::BitmapImage> load_bitmap(const std::string& path) {
    const std::optional<std::string> bytes = read_file(path);
    if (!bytes || bytes->empty()) return nullptr;
    auto image = std::make_shared<fizmo::images::BitmapImage>();
    if (!fizmo::images::decode_image(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size(), *image) || !image->is_valid_image()) return nullptr;
    return image;
}

struct DrawOp {
    enum class Kind { Fill, Stroke, Circle, CircleStroke, Text, Math, ClipPush, ClipPop, Image } kind = Kind::Fill;
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
    const fizmo::graphics::Texture* texture = nullptr;
    float alpha = 1.0f;
};

bool same_color(const bark::Rgba& a, const bark::Rgba& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

std::shared_ptr<fizmo::images::BitmapImage> make_color_wheel(unsigned size) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4u, 0);
    const double r = size / 2.0;
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
            const double dx = x + 0.5 - r;
            const double dy = y + 0.5 - r;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d > r) continue;
            ColorHsv hsv;
            hsv.h = std::fmod(std::atan2(-dy, dx) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
            hsv.s = std::min(1.0, d / (r - 1.0));
            hsv.v = 1.0;
            const bark::Rgba c = color_from_hsv(hsv);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4u;
            rgba[i] = c.r;
            rgba[i + 1] = c.g;
            rgba[i + 2] = c.b;
            rgba[i + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(r - d, 0.0, 1.0) * 255.0));
        }
    }
    return std::make_shared<fizmo::images::BitmapImage>(fizmo::images::BitmapImage::from_rgba(size, size, rgba.data()));
}

enum class PickerPart {
    None,
    Wheel,
    Value,
    Alpha
};

struct ColorSwatch {
    const Node* node = nullptr;
    Rect swatch;
    Rect box;
    bool code = false;
    std::size_t line = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string format;
};

struct ColorPicker {
    const Node* node = nullptr;
    ColorHsv hsv;
    std::uint8_t alpha = 255;
    bark::Rgba original;
    bark::Rgba last;
    std::string committed;
    PickerPart drag = PickerPart::None;
    bool show_alpha = true;
    Rect panel;
    float wheel_cx = 0;
    float wheel_cy = 0;
    float wheel_r = 0;
    Rect value_bar;
    Rect alpha_bar;
    std::vector<std::pair<Rect, std::string>> formats;
    bool code = false;
    std::size_t code_line = 0;
    std::size_t code_begin = 0;
    std::size_t code_end = 0;
    std::string code_format;
};

struct CodeHover {
    const Node* node = nullptr;
    CodePos pos;
    bool requested = false;
    bool shown = false;
    Rect anchor;
    Rect panel;
    std::vector<std::pair<Rect, std::string>> actions;
    const Node* pending = nullptr;
    CodePos pending_pos;
    std::uint64_t timer = 0;
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
    Rect clip = k_everywhere;

    bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h && clip.contains(px, py); }
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
    Rect clip = k_everywhere;

    bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h && clip.contains(px, py); }
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
        m_code_focus = nullptr;
        m_code_drag = nullptr;
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
        std::string page_title;
        mutable_node(m_window).walk([&page_title](const Node& n) {
            if (is_page(n) && n.has_attribute("title") && is_page_shown(n)) page_title = n.attribute_or("title", "");
        });
        if (!page_title.empty()) return page_title;
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

        poll_dialogs();
        if (m_picker.node && !document().is_attached(*m_picker.node)) m_picker = ColorPicker{};
        m_swatches.clear();
        m_ops.clear();
        m_hits.clear();
        m_boxes.clear();
        m_opacity.clear();
        m_scrollers.clear();
        m_clips.clear();
        const bark::ComputedStyle& style = style_of(m_window);
        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, 0, 0, w, h, to_color(*bg));
        const Box pad = box_of(style, "padding");
        float y = pad.top - m_scroll;
        const float start = y;
        m_avail_h = std::max(0.0f, h - pad.top - pad.bottom);
        lay_content(r, m_window, style, pad.left, y, w - pad.left - pad.right);
        m_avail_h = -1.0f;
        m_content_height = (y - start) + pad.top + pad.bottom;
        const float max_scroll = std::max(0.0f, m_content_height - h);
        if (m_scroll > max_scroll) m_scroll = max_scroll;
        layout_modals(r, w, h);
        layout_code_hover(r, w, h);
        layout_picker(r, w, h);
        std::vector<Rect> clips;
        auto apply = [&r](const Rect& c) {
            const int cx = static_cast<int>(std::floor(c.x));
            const int cy = static_cast<int>(std::floor(c.y));
            r.set_clip_rect(cx, cy, static_cast<unsigned>(std::max(0.0f, std::ceil(c.x + c.w) - static_cast<float>(cx))), static_cast<unsigned>(std::max(0.0f, std::ceil(c.y + c.h) - static_cast<float>(cy))));
        };
        for (const DrawOp& op : m_ops) {
            if (op.kind == DrawOp::Kind::ClipPush) {
                Rect c{ op.x, op.y, op.w, op.h };
                if (!clips.empty()) c = c.intersect(clips.back());
                clips.push_back(c);
                apply(c);
            } else if (op.kind == DrawOp::Kind::ClipPop) {
                if (!clips.empty()) clips.pop_back();
                if (clips.empty()) r.reset_clip_rect();
                else apply(clips.back());
            } else {
                draw(r, op);
            }
        }
        r.reset_clip_rect();
        report_cursors();
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
                if (m_code_drag) code_drag_to(px, py);
                if (m_picker.drag != PickerPart::None) picker_drag(px, py);
                hover_at(px, py);
                code_hover_move(px, py);
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
                if (dispatch(hit_element(px, py), ev) && e.button == 1) code_double_click(px, py);
                break;
            }
            case WindowEventType::MouseScroll: {
                const double dy = e.scroll_y != 0.0f ? static_cast<double>(e.scroll_y) : static_cast<double>(e.scroll_delta);
                Event ev = pointer_event("wheel", m_mouse_x, m_mouse_y, 0);
                ev.delta_x = e.scroll_x;
                ev.delta_y = dy;
                if (m_modal && !m_modal_panel.contains(m_mouse_x, m_mouse_y)) break;
                if (!(m_code_hover.shown && m_code_hover.panel.contains(m_mouse_x, m_mouse_y))) hide_code_hover();
                if (dispatch(hit_element(m_mouse_x, m_mouse_y), ev)) {
                    const bool horizontal = (m_mods & ModifierShift) != 0 || (e.scroll_x != 0.0f && e.scroll_y == 0.0f && e.scroll_delta == 0.0f);
                    const double amount = horizontal && e.scroll_x != 0.0f ? static_cast<double>(e.scroll_x) : dy;
                    if (!scroll_inner(m_mouse_x, m_mouse_y, static_cast<float>(amount) * 48.0f, horizontal) && !m_modal) m_scroll = std::max(0.0f, m_scroll - static_cast<float>(dy) * 48.0f);
                    hover_at(m_mouse_x, m_mouse_y);
                }
                break;
            }
            case WindowEventType::TextInput:
                hide_code_hover();
                type_text(e.text);
                break;
            case WindowEventType::KeyPress: {
                const fizmo::input::Key k = e.logical_key != fizmo::input::Key::Unknown ? e.logical_key : e.physical_key;
                const bool modifier_only = k == fizmo::input::Key::LeftControl || k == fizmo::input::Key::RightControl || k == fizmo::input::Key::LeftShift ||
                                           k == fizmo::input::Key::RightShift || k == fizmo::input::Key::LeftAlt || k == fizmo::input::Key::RightAlt ||
                                           k == fizmo::input::Key::LeftMeta || k == fizmo::input::Key::RightMeta;
                if (!modifier_only) hide_code_hover();
                key_down(e);
                hover_at(m_mouse_x, m_mouse_y);
                break;
            }
            case WindowEventType::KeyRelease: {
                hover_at(m_mouse_x, m_mouse_y);
                Event ev = key_event("key_up", e);
                dispatch(key_target(), ev);
                break;
            }
            default: break;
        }
    }

    bool focus_from_script(Node* node) {
        if (!node) {
            blur_code();
            set_focused(nullptr);
            return true;
        }
        if (!document().is_attached(*node) || is_disabled(node)) return false;
        const std::optional<InputKind> kind = input_kind_of(*node);
        if (kind && is_text_input(*kind)) focus(node);
        else if (kind && *kind == InputKind::Code) focus_code(node);
        else {
            blur_code();
            set_focused(node);
        }
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
            case DrawOp::Kind::Image: if (op.texture) r.draw_texture(*op.texture, x, y, w, h, op.alpha); break;
            case DrawOp::Kind::ClipPush:
            case DrawOp::Kind::ClipPop: break;
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
        if (style.text("font-style").value_or("") == "italic") ts.set_italic(true);
        const std::string decoration = style.text("text-decoration").value_or("");
        if (decoration.find("underline") != std::string::npos) ts.set_underline(true);
        if (decoration.find("line-through") != std::string::npos) ts.set_strikethrough(true);
        if (const auto family = style.text("font-family")) {
            const std::size_t comma = family->find(',');
            if (const auto font = font_named(family->substr(0, comma))) ts.set_font(*font);
        }
        return ts;
    }

    std::string style_key(const fizmo::text::TextStyle& style) {
        return std::to_string(style.size()) + (style.is_bold() ? "b" : "") + (style.is_italic() ? "i" : "") + std::to_string(static_cast<int>(style.font()));
    }

    float text_width(fizmo::graphics::Renderer& r, const std::string& text, const fizmo::text::TextStyle& style) {
        if (text.empty()) return 0.0f;
        std::string key = style_key(style);
        key += '\x1f';
        key += text;
        auto it = m_measure.find(key);
        if (it != m_measure.end()) return it->second;
        if (m_measure.size() > 40000) m_measure.clear();
        const float w = static_cast<float>(r.measure_text(text.c_str(), static_cast<int>(text.size()), style).width);
        m_measure.emplace(std::move(key), w);
        return w;
    }

    float line_height(fizmo::graphics::Renderer& r, const fizmo::text::TextStyle& style) {
        const std::string key = style_key(style) + "\x1eLH";
        auto it = m_measure.find(key);
        if (it != m_measure.end()) return it->second;
        const fizmo::text::TextMetrics m = r.measure_text("Hg", 2, style);
        const float h = static_cast<float>(std::max(m.height, 1u));
        m_measure.emplace(key, h);
        return h;
    }

    Rect current_clip() const { return m_clips.empty() ? k_everywhere : m_clips.back(); }

    std::size_t add_box(ElementBox box) {
        box.clip = current_clip();
        m_boxes.push_back(box);
        return m_boxes.size() - 1;
    }

    void add_hit(Hit hit) {
        hit.clip = current_clip();
        m_hits.push_back(hit);
    }

    void push_clip(const Rect& rect) {
        push(DrawOp::Kind::ClipPush, rect.x, rect.y, rect.w, rect.h, Color());
        m_clips.push_back(current_clip().intersect(rect));
    }

    void pop_clip() {
        push(DrawOp::Kind::ClipPop, 0, 0, 0, 0, Color());
        if (!m_clips.empty()) m_clips.pop_back();
    }

    struct Mark {
        std::size_t ops = 0;
        std::size_t hits = 0;
        std::size_t boxes = 0;
        std::size_t scrollers = 0;
        std::size_t clips = 0;
        std::size_t swatches = 0;
    };

    Mark mark() const { return Mark{ m_ops.size(), m_hits.size(), m_boxes.size(), m_scrollers.size(), m_clips.size(), m_swatches.size() }; }

    void rewind(const Mark& m) {
        m_ops.resize(m.ops);
        m_hits.resize(m.hits);
        m_boxes.resize(m.boxes);
        m_scrollers.resize(m.scrollers);
        m_clips.resize(m.clips);
        m_swatches.resize(m.swatches);
    }

    bool is_row(const Node& element, const bark::ComputedStyle& style) {
        const std::string display = style.text("display").value_or("");
        return display == "row" || (display.empty() && element.tag() == "row");
    }

    float flex_of(const bark::ComputedStyle& style) {
        const bark::ValueList* v = style.get("flex");
        if (!v || v->empty() || !(*v)[0].is_number()) return 0.0f;
        return std::max(0.0f, static_cast<float>((*v)[0].number));
    }

    bool is_inline_flow(const Node& node) {
        if (node.is_text()) return true;
        if (!node.is_element()) return false;
        const bark::ComputedStyle& style = style_of(node);
        const std::string display = style.text("display").value_or("");
        if (display == "inline") return node.tag() != "math" && !input_kind_of(node);
        if (!display.empty()) return false;
        if (node.tag() == "math" || node.tag() == "img") return false;
        return tag_role(node.tag()) == TagRole::Inline;
    }

    void lay_content(fizmo::graphics::Renderer& r, const Node& element, const bark::ComputedStyle& style, float x, float& y, float width) {
        if (is_row(element, style)) layout_row(r, element, style, x, y, width);
        else layout_children(r, element, x, y, width);
    }

    bool skipped(const Node& node) {
        if (node.is_text()) return false;
        if (!node.is_element()) return true;
        if (node.tag() == "window") return true;
        return style_of(node).text("display").value_or("") == "none";
    }

    void layout_children(fizmo::graphics::Renderer& r, const Node& parent, float x, float& y, float width) {
        std::vector<const Node*> blocks;
        std::vector<std::vector<const Node*>> runs;
        std::vector<int> order;
        std::vector<const Node*> run;
        auto flush = [&] {
            bool visible = false;
            for (const Node* n : run) visible = visible || !n->is_text() || !collapse_whitespace(n->text()).empty() || preserves_space(parent);
            if (!run.empty() && visible) {
                runs.push_back(run);
                order.push_back(-static_cast<int>(runs.size()));
            }
            run.clear();
        };
        for (const auto& child : parent.children()) {
            if (skipped(*child)) continue;
            if (is_inline_flow(*child)) {
                run.push_back(child.get());
                continue;
            }
            flush();
            blocks.push_back(child.get());
            order.push_back(static_cast<int>(blocks.size()));
        }
        flush();

        const bark::ComputedStyle& parent_style = style_of(parent);
        const Alignment al = alignment_of(parent_style, false);
        auto place_item = [&](int o, float& yy, float forced_h) {
            if (o < 0) {
                const std::vector<const Node*>& one_run = runs[static_cast<std::size_t>(-o - 1)];
                if (al.cross == "center" || al.cross == "end") {
                    const float rw = run_width(r, parent, one_run, width);
                    if (rw > 0.0f && rw < width) {
                        const float off = al.cross == "center" ? (width - rw) / 2.0f : width - rw;
                        layout_inline(r, parent, one_run, x + off, yy, rw);
                        return;
                    }
                }
                layout_inline(r, parent, one_run, x, yy, width);
            } else {
                place_block(r, *blocks[static_cast<std::size_t>(o - 1)], x, yy, width, al.cross, forced_h);
            }
        };

        std::vector<float> forced(blocks.size(), -1.0f);
        const float avail = m_avail_h;
        float total_flex = 0.0f;
        if (avail >= 0.0f) {
            for (const Node* b : blocks) total_flex += flex_of(style_of(*b));
            if (total_flex > 0.0f) {
                float used = 0.0f;
                const Mark before = mark();
                const float saved_avail = m_avail_h;
                for (int o : order) {
                    float my = 0.0f;
                    if (o < 0) {
                        place_item(o, my, -1.0f);
                    } else {
                        const Node* b = blocks[static_cast<std::size_t>(o - 1)];
                        const bark::ComputedStyle& bs = style_of(*b);
                        if (flex_of(bs) > 0.0f) {
                            const Box m = box_of(bs, "margin");
                            my = m.top + m.bottom;
                        } else {
                            m_avail_h = saved_avail;
                            place_item(o, my, -1.0f);
                        }
                    }
                    used += my;
                }
                rewind(before);
                m_avail_h = saved_avail;
                const float free = std::max(0.0f, avail - used);
                for (std::size_t i = 0; i < blocks.size(); ++i) {
                    const float f = flex_of(style_of(*blocks[i]));
                    if (f > 0.0f) forced[i] = free * f / total_flex;
                }
            }
        }

        std::vector<float> lead(order.size(), 0.0f);
        std::vector<float> trail(order.size(), 0.0f);
        if (avail >= 0.0f && total_flex <= 0.0f && !order.empty()) {
            std::size_t autos = 0;
            for (const Node* b : blocks) {
                const AutoSides a = auto_sides(style_of(*b), "margin");
                autos += (a.top ? 1u : 0u) + (a.bottom ? 1u : 0u);
            }
            if (autos > 0 || al.main != "start") {
                const Mark before = mark();
                float used = 0.0f;
                for (int o : order) {
                    float my = 0.0f;
                    m_avail_h = avail;
                    place_item(o, my, -1.0f);
                    used += my;
                }
                rewind(before);
                m_avail_h = avail;
                const float free = std::max(0.0f, avail - used);
                if (autos > 0) {
                    const float share = free / static_cast<float>(autos);
                    for (std::size_t k = 0; k < order.size(); ++k) {
                        if (order[k] < 0) continue;
                        const AutoSides a = auto_sides(style_of(*blocks[static_cast<std::size_t>(order[k] - 1)]), "margin");
                        if (a.top) lead[k] += share;
                        if (a.bottom) trail[k] += share;
                    }
                } else {
                    spread(al.main, free, lead, trail);
                }
            }
        }

        for (std::size_t k = 0; k < order.size(); ++k) {
            const int o = order[k];
            y += lead[k];
            m_avail_h = avail;
            place_item(o, y, o > 0 ? forced[static_cast<std::size_t>(o - 1)] : -1.0f);
            y += trail[k];
        }
        m_avail_h = avail;
    }

    float run_width(fizmo::graphics::Renderer& r, const Node& parent, const std::vector<const Node*>& run, float width) {
        float total = 0.0f;
        for (const Node* n : run) {
            const float w = natural_width(r, parent, *n, width);
            if (w < 0.0f) return -1.0f;
            total += w + 4.0f;
        }
        return total;
    }

    float fit_width(fizmo::graphics::Renderer& r, const Node& node, float width) {
        const float natural = natural_width(r, node.parent() ? *node.parent() : node, node, width);
        if (natural >= 0.0f) return natural;
        const bark::ComputedStyle& style = style_of(node);
        if (is_row(node, style)) return -1.0f;
        const Box pad = box_of(style, "padding");
        const Box margin = box_of(style, "margin");
        float best = 0.0f;
        float line = 0.0f;
        bool any = false;
        for (const auto& c : node.children()) {
            if (skipped(*c)) continue;
            if (c->is_text() && collapse_whitespace(c->text()).empty()) continue;
            if (c->is_text() || is_inline_flow(*c)) {
                const float w = natural_width(r, node, *c, width);
                if (w < 0.0f) return -1.0f;
                line += w + 4.0f;
            } else {
                best = std::max(best, line);
                line = 0.0f;
                const float w = fit_width(r, *c, width);
                if (w < 0.0f) return -1.0f;
                best = std::max(best, w);
            }
            any = true;
        }
        best = std::max(best, line);
        if (!any) return -1.0f;
        return best + pad.left + pad.right + margin.left + margin.right + 1.0f;
    }

    float outer_width(fizmo::graphics::Renderer& r, const Node& b, float width, bool fit) {
        const bark::ComputedStyle& s = style_of(b);
        const Box m = box_of(s, "margin");
        const float avail = std::max(0.0f, width - m.left - m.right);
        float bw = avail;
        const bool fixed_kind = b.tag() == "img" || input_kind_of(b).has_value();
        if (fixed_kind) {
            const float n = natural_width(r, b.parent() ? *b.parent() : b, b, width);
            if (n >= 0.0f) bw = std::min(avail, n - m.left - m.right);
            return bw + m.left + m.right;
        }
        if (const auto w = resolve_length(s, "width", width)) {
            bw = std::min(avail, *w);
        } else if (fit || is_inline_block(b, s)) {
            const float n = fit_width(r, b, width);
            if (n >= 0.0f) bw = std::min(avail, n - m.left - m.right);
        }
        if (const auto mx = resolve_length(s, "max-width", width)) bw = std::min(bw, *mx);
        if (const auto mn = resolve_length(s, "min-width", width)) bw = std::max(bw, std::min(*mn, avail));
        return bw + m.left + m.right;
    }

    void place_block(fizmo::graphics::Renderer& r, const Node& b, float x, float& y, float width, const std::string& cross, float forced) {
        const bark::ComputedStyle& bs = style_of(b);
        std::string self = cross;
        if (const auto s = bs.text("align-self")) {
            const std::string w = align_word(*s);
            if (!w.empty()) self = w;
        }
        const AutoSides au = auto_sides(bs, "margin");
        if (self == "stretch" && !au.left && !au.right) {
            layout_element(r, b, x, y, width, false, forced);
            return;
        }
        const float ow = outer_width(r, b, width, self != "stretch");
        const float free = std::max(0.0f, width - ow);
        float off = 0.0f;
        if (au.left && au.right) off = free / 2.0f;
        else if (au.left) off = free;
        else if (au.right) off = 0.0f;
        else if (self == "center") off = free / 2.0f;
        else if (self == "end") off = free;
        layout_element(r, b, x + std::round(off), y, ow, true, forced);
    }

    bool preserves_space(const Node& owner) {
        const std::string ws = style_of(owner).text("white-space").value_or("normal");
        return ws == "pre" || ws == "pre-wrap";
    }

    float natural_width(fizmo::graphics::Renderer& r, const Node& owner, const Node& node, float row_width) {
        if (node.is_text()) return text_width(r, collapse_whitespace(node.text()), text_style(owner)) + 1.0f;
        if (!node.is_element()) return 0.0f;
        const bark::ComputedStyle& style = style_of(node);
        if (style.text("display").value_or("") == "none") return 0.0f;
        const Box margin = box_of(style, "margin");
        const float margins = margin.left + margin.right;
        if (const std::optional<InputKind> kind = input_kind_of(node)) {
            if (*kind == InputKind::Code) {
                if (const auto w = resolve_length(style, "width", row_width)) return *w + margins;
                return -1.0f;
            }
            if (*kind == InputKind::File) {
                if (const auto w = resolve_length(style, "width", row_width)) return *w + margins;
                const Box pad = box_of(style, "padding");
                return text_width(r, file_label(node), text_style(node)) + pad.left + pad.right + margins + 2.0f + file_value_width(r, node);
            }
            if (is_text_input(*kind)) return resolve_length(style, "width", row_width).value_or(220.0f) + margins;
            if (*kind == InputKind::Slider) return resolve_length(style, "width", row_width).value_or(220.0f) + 70.0f + margins;
            const float size = std::round(static_cast<float>(font_size_of(node)) * 1.05f);
            const bool toggle = *kind == InputKind::Bool && is_switch(node, style.text("appearance"));
            const float box = toggle ? std::round(size * 1.8f) : size;
            const std::string label = collapse_whitespace(node.inner_text());
            return box + (label.empty() ? 0.0f : 8.0f + text_width(r, label, text_style(node))) + margins + 2.0f;
        }
        if (node.tag() == "img") {
            float iw = 0.0f;
            float ih = 0.0f;
            image_size(node, style, row_width, iw, ih);
            const Box pad = box_of(style, "padding");
            return iw + pad.left + pad.right + margins;
        }
        if (const auto w = resolve_length(style, "width", row_width)) return *w + margins;
        if (is_row(node, style) && !style.get("flex")) {
            const Box pad = box_of(style, "padding");
            const float gap = static_cast<float>(style.length("gap").value_or(8.0));
            float total = 0.0f;
            std::size_t n = 0;
            for (const auto& c : node.children()) {
                if (c->is_text() && collapse_whitespace(c->text()).empty()) continue;
                if (c->is_element() && skipped(*c)) continue;
                if (!c->is_element() && !c->is_text()) continue;
                const float cw = natural_width(r, node, *c, row_width);
                if (cw < 0.0f) return -1.0f;
                total += cw;
                ++n;
            }
            if (n == 0) return -1.0f;
            return total + gap * static_cast<float>(n - 1) + pad.left + pad.right + margins + 1.0f;
        }
        if (is_inline_block(node, style) || is_inline_flow(node)) {
            const Box pad = box_of(style, "padding");
            return text_width(r, collapse_whitespace(node.inner_text()), text_style(node)) + pad.left + pad.right + margins + 2.0f;
        }
        return -1.0f;
    }

    bool is_inline_block(const Node& node, const bark::ComputedStyle& style) {
        const std::string display = style.text("display").value_or("");
        return display == "inline-block" || (display.empty() && (node.tag() == "button" || node.tag() == "input_file"));
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
        const float spacing = gap * static_cast<float>(items.size() - 1);
        std::vector<float> widths;
        std::vector<float> weights;
        std::vector<float> mins;
        std::vector<float> maxs;
        float fixed = 0.0f;
        for (const Node* n : items) {
            const float w = natural_width(r, element, *n, width);
            widths.push_back(w);
            float weight = 1.0f;
            float lo = 0.0f;
            float hi = 1e9f;
            if (n->is_element()) {
                const bark::ComputedStyle& cs = style_of(*n);
                const Box m = box_of(cs, "margin");
                if (cs.get("flex")) weight = flex_of(cs);
                if (const auto v = resolve_length(cs, "min-width", width)) lo = *v + m.left + m.right;
                if (const auto v = resolve_length(cs, "max-width", width)) hi = *v + m.left + m.right;
            }
            weights.push_back(weight);
            mins.push_back(lo);
            maxs.push_back(std::max(lo, hi));
            if (w >= 0.0f) {
                widths.back() = std::clamp(w, lo, std::max(lo, hi));
                fixed += widths.back();
            }
        }
        std::vector<float> final_w = widths;
        std::vector<bool> settled(items.size(), false);
        for (std::size_t i = 0; i < items.size(); ++i) settled[i] = widths[i] >= 0.0f;
        float remaining = std::max(0.0f, width - fixed - spacing);
        for (int pass = 0; pass < 8; ++pass) {
            float total = 0.0f;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (!settled[i]) total += weights[i];
            }
            if (total <= 0.0f) {
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (!settled[i]) final_w[i] = mins[i];
                }
                break;
            }
            bool clamped = false;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (settled[i]) continue;
                const float share = remaining * weights[i] / total;
                if (share < mins[i] || share > maxs[i]) {
                    final_w[i] = std::clamp(share, mins[i], maxs[i]);
                    settled[i] = true;
                    remaining = std::max(0.0f, remaining - final_w[i]);
                    clamped = true;
                }
            }
            if (!clamped) {
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (!settled[i]) final_w[i] = remaining * weights[i] / total;
                }
                break;
            }
        }
        bool any_flexible = false;
        float used = spacing;
        for (std::size_t i = 0; i < items.size(); ++i) {
            any_flexible = any_flexible || widths[i] < 0.0f;
            used += final_w[i];
        }
        const Alignment al = alignment_of(style, true);
        std::vector<float> pre(items.size(), 0.0f);
        std::vector<float> post(items.size(), 0.0f);
        if (!any_flexible) {
            const float free = std::max(0.0f, width - used);
            std::size_t autos = 0;
            for (const Node* n : items) {
                if (!n->is_element()) continue;
                const AutoSides a = auto_sides(style_of(*n), "margin");
                autos += (a.left ? 1u : 0u) + (a.right ? 1u : 0u);
            }
            if (autos > 0) {
                const float share = free / static_cast<float>(autos);
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (!items[i]->is_element()) continue;
                    const AutoSides a = auto_sides(style_of(*items[i]), "margin");
                    if (a.left) pre[i] += share;
                    if (a.right) post[i] += share;
                }
            } else {
                spread(al.main, free, pre, post);
            }
        }
        const float row_h = m_avail_h;
        std::vector<std::string> self(items.size(), al.cross);
        bool measure = false;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i]->is_element()) {
                const bark::ComputedStyle& cs = style_of(*items[i]);
                if (const auto s = cs.text("align-self")) {
                    const std::string w = align_word(*s);
                    if (!w.empty()) self[i] = w;
                }
                const AutoSides a = auto_sides(cs, "margin");
                if (a.top && a.bottom) self[i] = "center";
                else if (a.top) self[i] = "end";
                else if (a.bottom) self[i] = "start";
            }
            if (self[i] != "stretch" && self[i] != "start") measure = true;
        }
        auto forced_for = [&](std::size_t i) {
            if (self[i] != "stretch" || row_h < 0.0f || !items[i]->is_element()) return -1.0f;
            const Box m = box_of(style_of(*items[i]), "margin");
            return std::max(0.0f, row_h - m.top - m.bottom);
        };
        auto lay_item = [&](std::size_t i, float px, float& py, float w) {
            m_avail_h = row_h;
            if (items[i]->is_text()) {
                std::vector<const Node*> one{ items[i] };
                layout_inline(r, element, one, px, py, w);
            } else {
                layout_element(r, *items[i], px, py, w, true, forced_for(i));
            }
        };
        std::vector<float> heights(items.size(), -1.0f);
        float row_height = row_h;
        if (measure) {
            const Mark before = mark();
            float tallest = 0.0f;
            for (std::size_t i = 0; i < items.size(); ++i) {
                float my = y;
                lay_item(i, x, my, final_w[i]);
                heights[i] = my - y;
                tallest = std::max(tallest, heights[i]);
            }
            rewind(before);
            if (row_height < 0.0f) row_height = tallest;
        }
        float cx = x;
        float bottom = y;
        for (std::size_t i = 0; i < items.size(); ++i) {
            cx += pre[i];
            const float w = std::min(final_w[i], std::max(0.0f, x + width - cx));
            float offset = 0.0f;
            if (measure && heights[i] >= 0.0f && self[i] != "stretch") {
                const float free = std::max(0.0f, row_height - heights[i]);
                if (self[i] == "center") offset = std::round(free / 2.0f);
                else if (self[i] == "end") offset = free;
            }
            float cy = y + offset;
            lay_item(i, cx, cy, w);
            bottom = std::max(bottom, cy);
            cx += w + gap + post[i];
        }
        if (measure && row_height >= 0.0f) bottom = std::max(bottom, y + row_height);
        m_avail_h = row_h;
        y = bottom;
    }

    struct Fragment {
        std::string text;
        fizmo::text::TextStyle style;
        const Node* node = nullptr;
        bool newline = false;
    };

    struct Piece {
        std::size_t fragment = 0;
        std::string text;
        float x = 0;
        float w = 0;
        bool space = false;
    };

    std::string collapse_inline(const std::string& text) {
        std::string out;
        bool space = false;
        for (char c : text) {
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                space = true;
                continue;
            }
            if (space) out += ' ';
            space = false;
            out += c;
        }
        if (space) out += ' ';
        return out;
    }

    void collect_fragments(const Node& owner, const Node& node, bool pre, std::vector<Fragment>& out) {
        if (node.is_text()) {
            Fragment f;
            f.style = text_style(owner);
            f.node = &owner;
            if (!pre) {
                f.text = collapse_inline(node.text());
                if (!f.text.empty()) out.push_back(std::move(f));
                return;
            }
            std::size_t start = 0;
            const std::string& t = node.text();
            for (std::size_t i = 0; i <= t.size(); ++i) {
                if (i == t.size() || t[i] == '\n') {
                    Fragment part = f;
                    part.text = t.substr(start, i - start);
                    if (!part.text.empty() && part.text.back() == '\r') part.text.pop_back();
                    if (!part.text.empty()) out.push_back(part);
                    if (i < t.size()) {
                        Fragment nl = f;
                        nl.newline = true;
                        out.push_back(nl);
                    }
                    start = i + 1;
                }
            }
            return;
        }
        if (!node.is_element() || skipped(node)) return;
        if (node.tag() == "br") {
            Fragment nl;
            nl.style = text_style(node);
            nl.node = &node;
            nl.newline = true;
            out.push_back(nl);
            return;
        }
        const bool inner_pre = preserves_space(node);
        for (const auto& c : node.children()) collect_fragments(node, *c, inner_pre, out);
    }

    void layout_inline(fizmo::graphics::Renderer& r, const Node& owner, const std::vector<const Node*>& nodes, float x, float& y, float width) {
        const bark::ComputedStyle& owner_style = style_of(owner);
        const std::string ws = owner_style.text("white-space").value_or("normal");
        const bool pre = ws == "pre" || ws == "pre-wrap";
        const bool wrap = ws != "pre" && ws != "nowrap";
        std::vector<Fragment> frags;
        for (const Node* n : nodes) collect_fragments(owner, *n, pre, frags);
        if (frags.empty()) return;
        if (!pre) {
            while (!frags.empty() && !frags.front().newline && frags.front().text == " ") frags.erase(frags.begin());
            if (!frags.empty() && !frags.front().newline && !frags.front().text.empty() && frags.front().text.front() == ' ') frags.front().text.erase(0, 1);
            if (frags.empty()) return;
        }

        const fizmo::text::TextStyle base = text_style(owner);
        const float base_h = line_height(r, base);
        std::vector<std::vector<Piece>> lines(1);
        std::vector<float> heights(1, 0.0f);
        float cursor = 0.0f;
        auto new_line = [&] {
            lines.emplace_back();
            heights.push_back(0.0f);
            cursor = 0.0f;
        };
        auto place = [&](std::size_t fi, const std::string& text, bool space) {
            const float w = text_width(r, text, frags[fi].style);
            if (space && lines.back().empty() && !pre) return;
            if (wrap && !space && cursor > 0.0f && cursor + w > width + 0.5f) {
                while (!lines.back().empty() && lines.back().back().space) {
                    cursor -= lines.back().back().w;
                    lines.back().pop_back();
                }
                new_line();
            }
            if (wrap && !space && w > width && cursor == 0.0f && width > 4.0f) {
                std::string chunk;
                for (const std::string& cp : split_code_points(text)) {
                    const float cw = text_width(r, chunk + cp, frags[fi].style);
                    if (!chunk.empty() && cw > width) {
                        const float pw = text_width(r, chunk, frags[fi].style);
                        lines.back().push_back(Piece{ fi, chunk, cursor, pw, false });
                        heights.back() = std::max(heights.back(), line_height(r, frags[fi].style));
                        new_line();
                        chunk.clear();
                    }
                    chunk += cp;
                }
                if (!chunk.empty()) {
                    const float pw = text_width(r, chunk, frags[fi].style);
                    lines.back().push_back(Piece{ fi, chunk, cursor, pw, false });
                    heights.back() = std::max(heights.back(), line_height(r, frags[fi].style));
                    cursor += pw;
                }
                return;
            }
            lines.back().push_back(Piece{ fi, text, cursor, w, space });
            heights.back() = std::max(heights.back(), line_height(r, frags[fi].style));
            cursor += w;
        };
        for (std::size_t fi = 0; fi < frags.size(); ++fi) {
            const Fragment& f = frags[fi];
            if (f.newline) {
                if (heights.back() == 0.0f) heights.back() = line_height(r, f.style);
                new_line();
                continue;
            }
            if (!wrap) {
                place(fi, f.text, false);
                continue;
            }
            std::string word;
            for (char c : f.text) {
                if (c == ' ') {
                    if (!word.empty()) place(fi, word, false);
                    word.clear();
                    place(fi, " ", true);
                } else {
                    word += c;
                }
            }
            if (!word.empty()) place(fi, word, false);
        }
        while (lines.size() > 1 && lines.back().empty() && !frags.back().newline) {
            lines.pop_back();
            heights.pop_back();
        }

        const std::string align = owner_style.text("text-align").value_or("left");
        for (std::size_t li = 0; li < lines.size(); ++li) {
            std::vector<Piece>& line = lines[li];
            if (!pre) {
                while (!line.empty() && line.back().space) line.pop_back();
            }
            const float lh = heights[li] > 0.0f ? heights[li] : base_h;
            float line_w = 0.0f;
            for (const Piece& p : line) line_w = std::max(line_w, p.x + p.w);
            float dx = x;
            if (align == "center") dx = x + std::max(0.0f, (width - line_w) / 2.0f);
            else if (align == "right") dx = x + std::max(0.0f, width - line_w);

            std::size_t i = 0;
            while (i < line.size()) {
                std::size_t j = i;
                std::string text;
                while (j < line.size() && line[j].fragment == line[i].fragment) {
                    text += line[j].text;
                    ++j;
                }
                const Fragment& f = frags[line[i].fragment];
                const float px = dx + line[i].x;
                const float pw = line[j - 1].x + line[j - 1].w - line[i].x;
                const float fh = line_height(r, f.style);
                const float py = y + (lh - fh);
                if (f.node && f.node != &owner) {
                    const bark::ComputedStyle& fs = style_of(*f.node);
                    if (const auto bg = fs.color("background")) {
                        const float radius = static_cast<float>(fs.length("border-radius").value_or(0.0));
                        push_rect(DrawOp::Kind::Fill, px - 2, py, pw + 4, fh, with_alpha(to_color(*bg), opacity_of(*f.node)), 1, radius);
                    }
                    add_box(ElementBox{ f.node, px, py, pw, fh });
                }
                push_text(px, py, pw + 2, fh, text, f.style);
                i = j;
            }
            y += lh;
        }
    }

    void layout_text(fizmo::graphics::Renderer& r, const Node& owner, const std::string& raw, float x, float& y, float width) {
        const std::string text = collapse_whitespace(raw);
        if (text.empty()) return;
        const fizmo::text::TextStyle style = text_style(owner);
        const float lh = line_height(r, style);
        float tx = x;
        const std::string align = style_of(owner).text("text-align").value_or("left");
        const float tw = text_width(r, text, style);
        if ((align == "center" || align == "right") && tw < width) tx = align == "center" ? x + (width - tw) / 2.0f : x + width - tw;
        push_text(tx, y, std::max(1.0f, width - (tx - x)), lh, text, style);
        y += lh;
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
        const auto w = resolve_length(style, "width", available);
        return w ? std::min(*w, available) : available;
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
            if (c.a < 255) {
                const float half = sw / 2.0f;
                push_rect(DrawOp::Kind::Fill, tx, y + pad.top + 2, sw, sw, Color(255, 255, 255, 255), 1, 3);
                push_rect(DrawOp::Kind::Fill, tx, y + pad.top + 2, half, half, Color(190, 190, 190, 255));
                push_rect(DrawOp::Kind::Fill, tx + half, y + pad.top + 2 + half, half, half, Color(190, 190, 190, 255));
            }
            push_rect(DrawOp::Kind::Fill, tx, y + pad.top + 2, sw, sw, to_color(c), 1, 3);
            push_rect(DrawOp::Kind::Stroke, tx, y + pad.top + 2, sw, sw, with_alpha(text_color(element), 0.4), 1, 3);
            if (!disabled && element.attribute_or("picker", "true") != "false") {
                ColorSwatch entry;
                entry.node = &element;
                entry.swatch = Rect{ x, y, tx + sw + 4 - x, h };
                entry.box = Rect{ x, y, w, h };
                m_swatches.push_back(entry);
            }
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
        if (focused && m_edit_all && !text.empty()) push_rect(DrawOp::Kind::Fill, tx - 1, y + pad.top + 1, std::min(text_width(r, text, ts) + 2, tw), lh - 2, with_alpha(accent_color(element), 0.35), 1, 2);
        if (focused) {
            const float cx = std::min(tx + text_width(r, text, ts) + 1, x + w - pad.right);
            push_rect(DrawOp::Kind::Fill, cx, y + pad.top + 2, 2, lh - 4, accent_color(element));
        }
        if (!disabled) add_hit(Hit{ &element, kind, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
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
        if (!disabled) add_hit(Hit{ &element, kind, x, y, w, row, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, row });
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
        if (!disabled) add_hit(Hit{ &element, InputKind::Slider, x, y, w, row, track_x, track_w });
        add_box(ElementBox{ &element, x, y, w, row });
        y += row;
    }

    std::string file_label(const Node& node) {
        std::string label = collapse_whitespace(node.inner_text());
        if (!label.empty()) return label;
        const std::string mode = node.attribute_or("mode", "open");
        if (mode == "folder") return "Choose folder\xE2\x80\xA6";
        if (mode == "save") return "Save as\xE2\x80\xA6";
        return node.has_attribute("multiple") ? "Choose files\xE2\x80\xA6" : "Choose file\xE2\x80\xA6";
    }

    std::string file_value_text(const Node& node) {
        if (node.has_attribute("hide-value")) return {};
        const std::string value = input_string(node).value_or("");
        if (value.empty()) return node.attribute_or("placeholder", "");
        std::vector<std::string> names;
        std::size_t start = 0;
        while (start <= value.size()) {
            const std::size_t end = value.find('\n', start);
            const std::string path = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!path.empty()) names.push_back(base_name(path));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (names.size() > 3) return std::to_string(names.size()) + " files";
        std::string out;
        for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
        return out;
    }

    float file_value_width(fizmo::graphics::Renderer& r, const Node& node) {
        const std::string text = file_value_text(node);
        return text.empty() ? 0.0f : 10.0f + text_width(r, text, text_style(node));
    }

    void layout_code(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, float forced_h) {
        const bark::ComputedStyle& style = style_of(element);
        CodeEditor& ed = m_code[&element];
        const std::string value = input_string(element).value_or("");
        if (value != ed.synced) {
            const CodePos keep = ed.buffer.caret();
            ed.buffer.set_text(value);
            ed.buffer.set_caret(keep);
            ed.synced = value;
        }
        ed.tab_size = static_cast<std::size_t>(std::max<std::int64_t>(1, parse_integer(element.attribute_or("tab-size", "4")).value_or(4)));
        const std::string caret_attr = element.attribute_or("caret", "");
        if (!caret_attr.empty() && caret_attr != ed.caret_request) {
            ed.caret_request = caret_attr;
            const std::string where = caret_attr.substr(0, caret_attr.find('#'));
            const std::size_t colon = where.find(':');
            const std::int64_t line = parse_integer(where.substr(0, colon)).value_or(1);
            const std::int64_t col = colon == std::string::npos ? 1 : parse_integer(where.substr(colon + 1)).value_or(1);
            const std::size_t li = static_cast<std::size_t>(std::max<std::int64_t>(1, line) - 1);
            const std::size_t safe = std::min(li, ed.buffer.line_count() - 1);
            ed.buffer.set_caret(CodePos{ safe, code_byte_column(ed.buffer.line(safe), static_cast<std::size_t>(std::max<std::int64_t>(1, col) - 1), ed.tab_size) });
            ed.reveal = true;
        }
        const Box pad = box_of(style, "padding");
        const float w = box_width(style, width);
        float h = forced_h >= 0.0f ? forced_h : resolve_length(style, "height", m_avail_h).value_or(300.0f);
        if (const auto mn = resolve_length(style, "min-height", m_avail_h)) h = std::max(h, *mn);
        if (const auto mx = resolve_length(style, "max-height", m_avail_h)) h = std::min(h, *mx);
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
        const bool focused = m_code_focus == &element;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);

        fizmo::text::TextStyle ts = text_style(element);
        if (!style.get("font-family")) ts.set_font(fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono));
        float lh = line_height(r, ts) * 1.3f;
        if (const bark::ValueList* lv = style.get("line-height"); lv && !lv->empty() && (*lv)[0].is_number()) {
            if ((*lv)[0].unit.empty()) lh = line_height(r, ts) * static_cast<float>((*lv)[0].number);
            else if (const auto px = style.length("line-height")) lh = static_cast<float>(*px);
        }
        lh = std::max(4.0f, std::round(lh));
        const float cw = std::max(1.0f, text_width(r, "MMMMMMMMMMMMMMMMMMMM", ts) / 20.0f);
        const bool numbers = element.attribute_or("line-numbers", "true") != "false";
        const std::size_t count = ed.buffer.line_count();
        const std::size_t digits = std::max<std::size_t>(3, std::to_string(count).size());
        const float gutter = numbers ? std::round(static_cast<float>(digits) * cw + cw * 2.0f) : 0.0f;
        const float inner_x = x + pad.left;
        const float inner_y = y + pad.top;
        const float inner_w = std::max(1.0f, w - pad.left - pad.right);
        const float inner_h = std::max(1.0f, h - pad.top - pad.bottom);
        const float text_x = inner_x + gutter + cw * 0.5f;
        const float view_w = std::max(1.0f, inner_w - gutter - cw * 0.5f);

        if (ed.states_revision != ed.buffer.revision() || ed.states.size() != count) {
            const CodeLanguage lang = code_language_from_name(element.attribute_or("language", "plain"));
            ed.states.assign(count, CodeLineState{});
            CodeLineState st;
            for (std::size_t i = 0; i < count; ++i) {
                ed.states[i] = st;
                highlight_code_line(lang, ed.buffer.line(i), st);
            }
            ed.states_revision = ed.buffer.revision();
        }

        const float content_h = static_cast<float>(count) * lh + inner_h * 0.5f;
        if (ed.reveal) {
            const CodePos c = ed.buffer.caret();
            const float cy = static_cast<float>(c.line) * lh;
            if (cy < ed.scroll_y) ed.scroll_y = cy;
            if (cy + lh > ed.scroll_y + inner_h) ed.scroll_y = cy + lh - inner_h;
            const float cx = static_cast<float>(code_display_column(ed.buffer.line(c.line), c.column, ed.tab_size)) * cw;
            if (cx < ed.scroll_x) ed.scroll_x = std::max(0.0f, cx - cw * 4.0f);
            if (cx + cw > ed.scroll_x + view_w) ed.scroll_x = cx + cw * 4.0f - view_w;
            ed.reveal = false;
        }
        const float max_y = std::max(0.0f, content_h - inner_h);
        ed.scroll_y = std::clamp(ed.scroll_y, 0.0f, max_y);
        ed.scroll_x = std::max(0.0f, ed.scroll_x);
        ed.text_x = text_x;
        ed.text_y = inner_y;
        ed.char_w = cw;
        ed.line_h = lh;
        ed.view_w = view_w;
        ed.view_h = inner_h;

        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, w, h, with_alpha(to_color(*bg), fade), 1, radius);
        push_clip(Rect{ x, y, w, h });
        const Color fg = with_alpha(raw_text_color(element), fade);
        const Color accent = accent_color(element);
        if (numbers) push_rect(DrawOp::Kind::Fill, inner_x, inner_y, gutter, inner_h, with_alpha(Color(0, 0, 0, 255), 0.12 * fade));

        const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(ed.scroll_y / lh)));
        const std::size_t last = std::min(count, first + static_cast<std::size_t>(std::ceil(inner_h / lh)) + 1);
        const CodePos caret = ed.buffer.caret();
        const std::vector<CodeCursor> cursors = ed.buffer.cursors();
        const CodeLanguage lang = code_language_from_name(element.attribute_or("language", "plain"));
        const std::vector<CodeTokenLook> looks = code_token_looks(style);
        const std::string spans_attr = element.attribute_or("semantic", "");
        if (spans_attr != ed.overlay_spans) {
            ed.overlay_spans = spans_attr;
            ed.overlay.set_spans(spans_attr);
        }
        const std::string problems_attr = element.attribute_or("problems", "");
        if (problems_attr != ed.overlay_problems) {
            ed.overlay_problems = problems_attr;
            ed.overlay.set_problems(problems_attr);
        }
        const CodeTokenLook error_line = code_extra_look(style, "code-error-line");
        const std::string mark_text = element.attribute_or("mark", "");
        auto marked = [&](std::string_view word) {
            std::size_t i = 0;
            while (i < mark_text.size()) {
                while (i < mark_text.size() && (mark_text[i] == ' ' || mark_text[i] == ',')) ++i;
                std::size_t end = i;
                while (end < mark_text.size() && mark_text[end] != ' ' && mark_text[end] != ',') ++end;
                if (std::string_view(mark_text).substr(i, end - i) == word) return true;
                i = end;
            }
            return false;
        };
        const bool swatches = (lang == CodeLanguage::Bark || lang == CodeLanguage::Grove) && element.attribute_or("color-swatches", "true") != "false";
        std::optional<std::pair<CodePos, std::size_t>> link_under;
        if ((m_mods & (ModifierControl | ModifierMeta)) != 0 && !m_code_drag && Rect{ inner_x + gutter, inner_y, inner_w - gutter, inner_h }.contains(m_mouse_x, m_mouse_y)) {
            if (const auto at = code_char_at(ed, m_mouse_x, m_mouse_y)) {
                if (const auto span = code_link_span(element, ed, *at)) link_under = std::make_pair(CodePos{ at->line, span->first }, span->second);
            }
        }
        const bool mark_errors = marked("error-underline");
        const bool mark_warnings = marked("warning-underline");
        const bool mark_error_lines = marked("error-line");
        const Color error_color = to_color(style.color("code-error-underline").value_or(bark::Rgba{ 241, 76, 76, 255 }));
        const Color warning_color = to_color(style.color("code-warning-underline").value_or(bark::Rgba{ 204, 167, 0, 255 }));
        const std::vector<CodeTokenKind> marks = code_marks(element.attribute_or("mark", ""));
        const Color mark_color = to_color(style.color("code-mark").value_or(bark::Rgba{ 255, 166, 77, 110 }));
        fizmo::text::TextStyle number_style = ts;
        number_style.set_bold(false);
        number_style.set_italic(false);
        number_style.set_underline(false);

        push_clip(Rect{ inner_x, inner_y, inner_w, inner_h });
        for (std::size_t li = first; li < last; ++li) {
            const float ly = inner_y + static_cast<float>(li) * lh - ed.scroll_y;
            const std::string& line = ed.buffer.line(li);
            bool caret_line = false;
            for (const CodeCursor& cur : cursors) caret_line = caret_line || (cur.caret.line == li && !cur.has_selection());
            if (focused && caret_line) push_rect(DrawOp::Kind::Fill, inner_x + gutter, ly, inner_w - gutter, lh, with_alpha(Color(255, 255, 255, 255), 0.04));
            for (const CodeCursor& cur : cursors) {
                if (!cur.has_selection()) continue;
                const CodePos sel_a = cur.start();
                const CodePos sel_b = cur.end();
                if (li < sel_a.line || li > sel_b.line) continue;
                const std::size_t from = li == sel_a.line ? code_display_column(line, sel_a.column, ed.tab_size) : 0;
                const std::size_t to = li == sel_b.line ? code_display_column(line, sel_b.column, ed.tab_size) : code_display_column(line, line.size(), ed.tab_size) + 1;
                const float sx = text_x + static_cast<float>(from) * cw - ed.scroll_x;
                const float sw = static_cast<float>(to - std::min(to, from)) * cw;
                push_rect(DrawOp::Kind::Fill, sx, ly, sw, lh, with_alpha(accent, focused ? 0.35 : 0.2));
            }
        }
        push_clip(Rect{ inner_x + gutter, inner_y, inner_w - gutter, inner_h });
        for (std::size_t li = first; li < last; ++li) {
            const float ly = inner_y + static_cast<float>(li) * lh - ed.scroll_y + (lh - line_height(r, ts)) / 2.0f;
            const std::string& line = ed.buffer.line(li);
            CodeLineState st = ed.states[li];
            std::vector<CodeToken> line_tokens = highlight_code_line(lang, line, st);
            if (!ed.overlay.empty()) line_tokens = overlay_code_tokens(line_tokens, ed.overlay.spans(li, line), line.size());
            const bool error_here = !ed.overlay.empty() && ed.overlay.has_error(li, line);
            for (const CodeToken& t : line_tokens) {
                const std::string piece = line.substr(t.begin, t.end - t.begin);
                if (piece.find_first_not_of(" \t") == std::string::npos) continue;
                const std::size_t col = code_display_column(line, t.begin, ed.tab_size);
                const float px = text_x + static_cast<float>(col) * cw - ed.scroll_x;
                if (px > text_x + view_w + cw) break;
                const std::string shown = code_expand_tabs(piece, ed.tab_size);
                const float pw = static_cast<float>(code_display_column(line, t.end, ed.tab_size) - col) * cw;
                if (px + pw < text_x - cw) continue;
                fizmo::text::TextStyle tok = ts;
                CodeTokenLook look = looks[static_cast<std::size_t>(t.kind)];
                if (error_here) {
                    if (error_line.color) look.color = error_line.color;
                    look.bold = look.bold || error_line.bold;
                    look.italic = look.italic || error_line.italic;
                    look.underline = look.underline || error_line.underline;
                }
                const Color c = look.color.value_or(fg);
                if (look.bold) tok.set_bold(true);
                if (look.italic) tok.set_italic(true);
                if (look.underline) tok.set_underline(true);
                if (std::find(marks.begin(), marks.end(), t.kind) != marks.end()) {
                    const std::size_t lead = piece.find_first_not_of(" \t");
                    const std::size_t trail = piece.find_last_not_of(" \t");
                    const float mx = px + static_cast<float>(code_display_column(line, t.begin + lead, ed.tab_size) - col) * cw;
                    const float mw = static_cast<float>(code_display_column(line, t.begin + trail + 1, ed.tab_size) - code_display_column(line, t.begin + lead, ed.tab_size)) * cw;
                    push_rect(DrawOp::Kind::Fill, mx - 2, ly - 1, mw + 4, line_height(r, ts) + 2, with_alpha(mark_color, fade), 1, 3);
                }
                tok.set_fg_color(with_alpha(c, fade));
                push_text(px, ly, pw + cw, lh, shown, tok);
            }
            const float row_y = inner_y + static_cast<float>(li) * lh - ed.scroll_y;
            if (link_under && link_under->first.line == li) {
                const std::size_t lb = link_under->first.column;
                const std::size_t le = link_under->second;
                Color under = fg;
                for (const CodeToken& t : line_tokens) {
                    if (t.begin <= lb && lb < t.end) under = looks[static_cast<std::size_t>(t.kind)].color.value_or(fg);
                }
                const float ux = text_x + static_cast<float>(code_display_column(line, lb, ed.tab_size)) * cw - ed.scroll_x;
                const float uw = static_cast<float>(code_display_column(line, le, ed.tab_size) - code_display_column(line, lb, ed.tab_size)) * cw;
                push_rect(DrawOp::Kind::Fill, ux, ly + line_height(r, ts) - 1.0f, uw, 1.0f, with_alpha(under, fade));
            }
            if (swatches) {
                for (const CodeColor& cc : find_code_colors(line)) {
                    const float size = std::max(6.0f, std::round(std::min(lh, cw * 1.6f) * 0.62f));
                    const float lit_x = text_x + static_cast<float>(code_display_column(line, cc.begin, ed.tab_size)) * cw - ed.scroll_x;
                    const float lit_w = static_cast<float>(code_display_column(line, cc.end, ed.tab_size) - code_display_column(line, cc.begin, ed.tab_size)) * cw;
                    Rect box{ lit_x, row_y, lit_w, lh };
                    Rect chip;
                    if (cc.begin > 0 && line[cc.begin - 1] == ' ') chip = Rect{ lit_x - cw + (cw - size) / 2.0f, row_y + (lh - size) / 2.0f, size, size };
                    else chip = Rect{ lit_x, row_y + lh - 3.0f, lit_w, 3.0f };
                    if (chip.x < text_x - 1.0f || chip.x + chip.w > text_x + view_w || chip.y < inner_y || chip.y + chip.h > inner_y + inner_h) continue;
                    const Color c = to_color(cc.color);
                    if (cc.color.a < 255) push_rect(DrawOp::Kind::Fill, chip.x, chip.y, chip.w, chip.h, Color(255, 255, 255, 255), 1, 2);
                    push_rect(DrawOp::Kind::Fill, chip.x, chip.y, chip.w, chip.h, with_alpha(c, fade), 1, 2);
                    push_rect(DrawOp::Kind::Stroke, chip.x, chip.y, chip.w, chip.h, with_alpha(fg, 0.45 * fade), 1, 2);
                    ColorSwatch sw;
                    sw.node = &element;
                    sw.swatch = Rect{ chip.x - 2, chip.y - 2, chip.w + 4, chip.h + 4 };
                    sw.box = box;
                    sw.code = true;
                    sw.line = li;
                    sw.begin = cc.begin;
                    sw.end = cc.end;
                    sw.format = cc.format;
                    if (!disabled && !code_read_only(element)) m_swatches.push_back(sw);
                }
            }
            if (ed.overlay.empty()) continue;
            if (error_here && mark_error_lines) {
                const std::size_t lead = line.find_first_not_of(" \t");
                if (lead != std::string::npos) {
                    const float mx = text_x + static_cast<float>(code_display_column(line, lead, ed.tab_size)) * cw - ed.scroll_x;
                    const float mw = static_cast<float>(code_display_column(line, line.size(), ed.tab_size) - code_display_column(line, lead, ed.tab_size)) * cw;
                    push_rect(DrawOp::Kind::Fill, mx - 2, ly - 1, mw + 4, line_height(r, ts) + 2, with_alpha(mark_color, fade), 1, 3);
                }
            }
            for (const CodeProblemMark& p : ed.overlay.problems(li, line)) {
                const std::size_t from = std::min(p.begin, line.size());
                const std::size_t to = std::min(std::max(p.end, from + 1), std::max(line.size(), from + 1));
                const float sx = text_x + static_cast<float>(code_display_column(line, from, ed.tab_size)) * cw - ed.scroll_x;
                const float sw = std::max(cw, static_cast<float>(code_display_column(line, std::min(to, line.size()), ed.tab_size) - code_display_column(line, from, ed.tab_size)) * cw);
                const float base_y = inner_y + static_cast<float>(li) * lh - ed.scroll_y + lh - 3.0f;
                const Color squiggle = with_alpha(p.kind == CodeProblemKind::Warning ? warning_color : error_color, fade);
                if (p.kind == CodeProblemKind::Warning ? mark_warnings : mark_errors) push_rect(DrawOp::Kind::Fill, sx - 2, ly - 1, sw + 4, line_height(r, ts) + 2, with_alpha(mark_color, fade), 1, 3);
                for (float sx2 = 0; sx2 < sw; sx2 += 2.0f) {
                    const int phase = static_cast<int>(sx2 / 2.0f) % 4;
                    const float dy = phase == 0 ? 0.0f : phase == 2 ? 2.0f : 1.0f;
                    push_rect(DrawOp::Kind::Fill, sx + sx2, base_y + dy - 1.0f, std::min(2.0f, sw - sx2), 1.0f, squiggle);
                }
            }
        }
        if (count == 1 && ed.buffer.line(0).empty() && element.has_attribute("placeholder")) {
            fizmo::text::TextStyle muted = ts;
            muted.set_fg_color(with_alpha(fg, 0.45));
            push_text(text_x, inner_y, view_w, lh, element.attribute_or("placeholder", ""), muted);
        }
        if (focused) {
            for (const CodeCursor& cur : cursors) {
                const float cx = text_x + static_cast<float>(code_display_column(ed.buffer.line(cur.caret.line), cur.caret.column, ed.tab_size)) * cw - ed.scroll_x;
                const float cy = inner_y + static_cast<float>(cur.caret.line) * lh - ed.scroll_y;
                push_rect(DrawOp::Kind::Fill, cx - 1, cy + 1, 2, lh - 2, accent);
            }
        }
        pop_clip();
        if (numbers) {
            for (std::size_t li = first; li < last; ++li) {
                const float ly = inner_y + static_cast<float>(li) * lh - ed.scroll_y + (lh - line_height(r, number_style)) / 2.0f;
                const std::string n = std::to_string(li + 1);
                const float nx = inner_x + gutter - cw - static_cast<float>(n.size()) * cw;
                number_style.set_fg_color(with_alpha(fg, li == caret.line && focused ? 0.85 : 0.35));
                push_text(nx, ly, static_cast<float>(n.size()) * cw + cw, lh, n, number_style);
            }
        }
        pop_clip();
        if (static_cast<float>(count) * lh > inner_h) {
            const float track = inner_h;
            const float thumb = std::max(24.0f, track * inner_h / (content_h));
            const float ty = inner_y + (track - thumb) * (ed.scroll_y / max_y);
            push_rect(DrawOp::Kind::Fill, x + w - 7, ty, 4, thumb, with_alpha(fg, 0.25), 1, 2);
        }
        pop_clip();
        Color border = to_color(style.color("border-color").value_or(bark::Rgba{ 255, 255, 255, 40 }));
        if (focused) border = accent;
        if (style.get("border-color") || focused) push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(border, fade), focused ? 2u : static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);

        if (!disabled) add_hit(Hit{ &element, InputKind::Code, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
        m_scrollers.push_back(Scroller{ &element, current_clip().intersect(Rect{ x, y, w, h }), max_y, true });
        y += h;
    }

    const fizmo::graphics::Texture* texture_for(const Node& img) {
        const std::string path = image_path(m_project, img);
        if (path.empty()) return nullptr;
        auto it = m_textures.find(path);
        if (it == m_textures.end()) {
            std::shared_ptr<fizmo::graphics::Texture> tex;
            if (auto bitmap = load_bitmap(path)) tex = std::make_shared<fizmo::graphics::Texture>(std::shared_ptr<const fizmo::images::BitmapImage>(bitmap), fizmo::graphics::SampleFilter::Bilinear);
            it = m_textures.emplace(path, tex).first;
        }
        return it->second && it->second->valid() ? it->second.get() : nullptr;
    }

    void image_size(const Node& img, const bark::ComputedStyle& style, float available, float& w, float& h) {
        const fizmo::graphics::Texture* tex = texture_for(img);
        const float nw = tex ? static_cast<float>(tex->width()) : 0.0f;
        const float nh = tex ? static_cast<float>(tex->height()) : 0.0f;
        std::optional<float> sw = resolve_length(style, "width", available);
        std::optional<float> sh = resolve_length(style, "height", -1.0f);
        if (!sw) {
            if (const auto a = parse_float(img.attribute_or("width", ""))) sw = static_cast<float>(*a);
        }
        if (!sh) {
            if (const auto a = parse_float(img.attribute_or("height", ""))) sh = static_cast<float>(*a);
        }
        if (sw && sh) {
            w = *sw;
            h = *sh;
        } else if (sw) {
            w = *sw;
            h = nw > 0.0f ? *sw * nh / nw : *sw;
        } else if (sh) {
            h = *sh;
            w = nh > 0.0f ? *sh * nw / nh : *sh;
        } else {
            w = nw;
            h = nh;
        }
        if (w > available && available > 0.0f && w > 0.0f) {
            h = h * available / w;
            w = available;
        }
    }

    void layout_image(const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        float w = 0.0f;
        float h = 0.0f;
        image_size(element, style, width, w, h);
        if (const fizmo::graphics::Texture* tex = texture_for(element)) {
            DrawOp& op = push(DrawOp::Kind::Image, x, y, w, h, Color());
            op.texture = tex;
            op.alpha = static_cast<float>(opacity_of(element));
        } else if (w > 0.0f && h > 0.0f) {
            push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(raw_text_color(element), 0.3));
        }
        y += h;
    }

    void layout_element(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, bool sized = false, float forced_h = -1.0f) {
        if (element.tag() == "window") return;
        const bark::ComputedStyle& style = style_of(element);
        if (style.text("display").value_or("") == "none") return;

        const Box margin = box_of(style, "margin");
        y += margin.top;
        const float bx = x + margin.left;
        float bw = width - margin.left - margin.right;
        const std::optional<InputKind> kind = input_kind_of(element);

        if (kind && *kind != InputKind::File) {
            if (*kind == InputKind::Code) {
                const float saved = m_avail_h;
                layout_code(r, element, bx, y, bw, forced_h);
                m_avail_h = saved;
            } else if (is_text_input(*kind)) layout_text_input(r, element, *kind, bx, y, bw);
            else if (*kind == InputKind::Slider) layout_slider(r, element, bx, y, bw);
            else layout_check(r, element, *kind, bx, y, bw);
            y += margin.bottom;
            if (auto gap = style.length("gap")) y += static_cast<float>(*gap);
            return;
        }

        if (element.tag() == "img") {
            float iw = 0.0f;
            float ih = 0.0f;
            image_size(element, style, bw, iw, ih);
            const Box ipad = box_of(style, "padding");
            bw = std::min(bw, iw + ipad.left + ipad.right);
        } else if (!sized) {
            if (const auto w = resolve_length(style, "width", width)) bw = std::min(bw, *w);
            else if (is_inline_block(element, style)) {
                const float natural = natural_width(r, *element.parent(), element, width) - margin.left - margin.right;
                if (natural > 0.0f) bw = std::min(bw, natural);
            }
            if (const auto mx = resolve_length(style, "max-width", width)) bw = std::min(bw, *mx);
            if (const auto mn = resolve_length(style, "min-width", width)) bw = std::max(bw, std::min(*mn, width - margin.left - margin.right));
        }

        const Box pad = box_of(style, "padding");
        const float content_x = bx + pad.left;
        const float content_w = std::max(0.0f, bw - pad.left - pad.right);
        const float top = y;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
        const std::size_t box_index = add_box(ElementBox{ &element, bx, top, bw, 0 });

        std::optional<float> fixed_h;
        if (forced_h >= 0.0f && element.tag() != "img") fixed_h = forced_h;
        else if (element.tag() == "img") fixed_h = std::nullopt;
        else if (const auto hgt = resolve_length(style, "height", m_avail_h)) fixed_h = *hgt;
        std::optional<float> max_h = resolve_length(style, "max-height", m_avail_h);
        const std::string overflow = style.text("overflow").value_or("visible");
        const bool clips = overflow == "hidden" || overflow == "scroll" || overflow == "auto";
        const bool scrolls = overflow == "scroll" || overflow == "auto";

        const auto bg = style.color("background");
        const auto border = style.color("border-color");
        const double opacity = opacity_of(element);
        const std::size_t bg_op = bg ? push_rect(DrawOp::Kind::Fill, bx, top, bw, 0, with_alpha(to_color(*bg), opacity), 1, radius) : std::size_t(-1);

        std::size_t clip_op = std::size_t(-1);
        if (clips) {
            const float clip_h = fixed_h ? *fixed_h : max_h ? *max_h : 1e7f;
            clip_op = m_ops.size();
            push_clip(Rect{ bx, top, bw, clip_h });
        }
        const float scroll = scrolls ? m_elem_scroll[&element] : 0.0f;
        const float saved_avail = m_avail_h;
        m_avail_h = fixed_h ? std::max(0.0f, *fixed_h - pad.top - pad.bottom) : -1.0f;

        float cy = y + pad.top - scroll;
        if (element.tag() == "math") layout_math(element, content_x, cy, content_w);
        else if (element.tag() == "img") layout_image(element, content_x, cy, content_w);
        else if (element.tag() == "br") cy += static_cast<float>(font_size_of(element));
        else if (element.tag() == "hr") {
            push_rect(DrawOp::Kind::Fill, content_x, cy + 4, content_w, 1, to_color(style.color("color").value_or(bark::Rgba{ 120, 120, 120, 255 })));
            cy += 9;
        } else if (kind && *kind == InputKind::File) {
            layout_file_content(r, element, content_x, cy, content_w);
        } else lay_content(r, element, style, content_x, cy, content_w);
        m_avail_h = saved_avail;

        const float natural_bottom = cy + scroll + pad.bottom;
        float bottom = natural_bottom;
        if (fixed_h) bottom = clips ? top + *fixed_h : std::max(bottom, top + *fixed_h);
        if (const auto mh = resolve_length(style, "min-height", saved_avail)) bottom = std::max(bottom, top + *mh);
        if (max_h && clips) bottom = std::min(bottom, top + *max_h);

        if (clips) {
            m_ops[clip_op].h = bottom - top;
            if (scrolls) {
                const float max_scroll = std::max(0.0f, natural_bottom - bottom);
                float& s = m_elem_scroll[&element];
                s = std::clamp(s, 0.0f, max_scroll);
                if (max_scroll > 0.0f) {
                    const float track = bottom - top;
                    const float thumb = std::max(20.0f, track * track / (track + max_scroll));
                    const float ty = top + (track - thumb) * (s / max_scroll);
                    push_rect(DrawOp::Kind::Fill, bx + bw - 6, ty, 4, thumb, with_alpha(raw_text_color(element), 0.25), 1, 2);
                }
                m_scrollers.push_back(Scroller{ &element, current_clip(), max_scroll, false });
            }
            pop_clip();
        }

        if (bg) m_ops[bg_op].h = bottom - top;
        if (border) push_rect(DrawOp::Kind::Stroke, bx, top, bw, bottom - top, with_alpha(to_color(*border), opacity), static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);
        m_boxes[box_index].h = bottom - top;
        if (kind && *kind == InputKind::File && !is_disabled(&element)) add_hit(Hit{ &element, InputKind::File, bx, top, bw, bottom - top, 0, 0 });
        y = bottom + margin.bottom;
        if (auto gap = style.length("gap")) {
            if (!is_row(element, style)) y += static_cast<float>(*gap);
        }
    }

    void layout_file_content(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        const std::string label = file_label(element);
        const float lw = text_width(r, label, ts);
        const std::string align = style_of(element).text("text-align").value_or("left");
        const std::string value = file_value_text(element);
        const float vw = value.empty() ? 0.0f : text_width(r, value, ts) + 10.0f;
        float lx = x;
        if (align == "center") lx = x + std::max(0.0f, (width - lw - vw) / 2.0f);
        push_text(lx, y, std::max(1.0f, std::min(lw + 2.0f, width)), lh, label, ts);
        if (!value.empty() && width > lw + 20.0f) {
            fizmo::text::TextStyle muted = ts;
            muted.set_fg_color(with_alpha(text_color(element), 0.6));
            muted.set_bold(false);
            push_text(lx + lw + 10.0f, y, std::max(1.0f, width - lw - 10.0f), lh, value, muted);
        }
        y += lh;
    }

    const Node* hit_element(float px, float py) const {
        const std::size_t floor = m_modal ? m_modal_boxes : 0;
        for (std::size_t i = m_boxes.size(); i > floor; --i) {
            if (m_boxes[i - 1].contains(px, py)) return m_boxes[i - 1].node;
        }
        return m_modal ? m_modal : &m_window;
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
            if (kind && *kind == InputKind::Code) {
                name = "text";
                if ((m_mods & (ModifierControl | ModifierMeta)) != 0 && code_node_at(m_mouse_x, m_mouse_y) == target) {
                    CodeEditor& ed = m_code[target];
                    if (const auto at = code_char_at(ed, m_mouse_x, m_mouse_y)) {
                        if (code_link_span(*target, ed, *at)) name = "pointer";
                    }
                }
                if (const ColorSwatch* sw = swatch_at(m_mouse_x, m_mouse_y); sw && sw->node == target) name = "pointer";
            }
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
        blur_code();
        blur(true);
        m_focus = node;
        m_edit = input_text(*node);
        m_focus_start = m_edit;
        m_invalid = false;
        m_edit_all = input_kind_of(*node) == InputKind::Color && !m_edit.empty();
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
        m_edit_all = false;
        if (m_native) m_native->stop_text_input();
    }

    void layout_modals(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        const Node* was = m_modal;
        m_modal = nullptr;
        m_modal_boxes = 0;
        m_modal_hits = 0;
        std::vector<const Node*> open;
        std::function<void(const Node&)> collect = [&](const Node& n) {
            for (const auto& c : n.children()) {
                if (!c->is_element() || c->tag() == "window") continue;
                if (is_modal(*c) && c->has_attribute("open") && is_page_shown(*c)) open.push_back(c.get());
                collect(*c);
            }
        };
        collect(m_window);
        for (const Node* modal : open) {
            const bark::ComputedStyle& style = style_of(*modal);
            push_rect(DrawOp::Kind::Fill, 0, 0, view_w, view_h, to_color(style.color("backdrop").value_or(bark::Rgba{ 0, 0, 0, 140 })));
            const Box pad = box_of(style, "padding");
            float width = resolve_length(style, "width", view_w).value_or(480.0f);
            if (const auto mn = resolve_length(style, "min-width", view_w)) width = std::max(width, *mn);
            if (const auto mx = resolve_length(style, "max-width", view_w)) width = std::min(width, *mx);
            width = std::max(80.0f, std::min(width, view_w - 24.0f));
            float max_h = view_h - 24.0f;
            if (const auto mx = resolve_length(style, "max-height", view_h)) max_h = std::min(max_h, *mx);
            const float inner_w = std::max(1.0f, width - pad.left - pad.right);
            const float saved_avail = m_avail_h;
            m_avail_h = std::max(0.0f, max_h - pad.top - pad.bottom);
            const Mark before = mark();
            float my = 0.0f;
            lay_content(r, *modal, style, pad.left, my, inner_w);
            rewind(before);
            float height = my + pad.top + pad.bottom;
            if (const auto fixed = resolve_length(style, "height", view_h)) height = *fixed;
            if (const auto mn = resolve_length(style, "min-height", view_h)) height = std::max(height, *mn);
            height = std::min(height, max_h);
            const float x = std::round((view_w - width) / 2.0f);
            const float y = std::round(std::max(12.0f, (view_h - height) / 2.0f));
            const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
            push_rect(DrawOp::Kind::Fill, x + 3, y + 6, width, height, Color(0, 0, 0, 90), 1, radius);
            if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, width, height, to_color(*bg), 1, radius);
            if (auto border = style.color("border-color")) push_rect(DrawOp::Kind::Stroke, x, y, width, height, to_color(*border), static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);
            m_modal_boxes = m_boxes.size();
            m_modal_hits = m_hits.size();
            add_box(ElementBox{ modal, x, y, width, height });
            push_clip(Rect{ x, y, width, height });
            float cy = y + pad.top;
            lay_content(r, *modal, style, x + pad.left, cy, inner_w);
            pop_clip();
            m_avail_h = saved_avail;
            m_modal = modal;
            m_modal_panel = Rect{ x, y, width, height };
        }
        if (m_modal && m_modal != was) {
            if (m_focus && !m_focus->is_inside(m_modal)) blur(true);
            if (m_code_focus && !m_code_focus->is_inside(m_modal)) blur_code();
            if (m_focused && !m_focused->is_inside(m_modal)) set_focused(nullptr);
        }
    }

    std::size_t live_hits() const { return m_modal ? m_modal_hits : 0; }

    void follow_link(const Node* from) {
        if (from && !is_disabled(from)) grove::follow_link(document(), &mutable_node(*from));
    }

    const ColorSwatch* swatch_at(float px, float py) const {
        for (const ColorSwatch& s : m_swatches) {
            if (s.swatch.contains(px, py)) return &s;
        }
        return nullptr;
    }

    void open_picker(const Node* node) {
        m_picker = ColorPicker{};
        const bark::Rgba c = input_color(*node).value_or(bark::Rgba{});
        m_picker.node = node;
        m_picker.hsv = color_to_hsv(c);
        m_picker.alpha = c.a;
        m_picker.original = c;
        m_picker.last = c;
        m_picker.committed = input_text(*node);
        m_picker.show_alpha = node->attribute_or("alpha", "true") != "false";
    }

    void picker_change() {
        if (!m_picker.node || m_picker.code) return;
        const std::string now = input_text(*m_picker.node);
        if (now == m_picker.committed) return;
        m_picker.committed = now;
        Event change;
        change.type = "change";
        dispatch(m_picker.node, change);
    }

    void close_picker(bool keep) {
        if (!m_picker.node) return;
        if (!keep && m_picker.code) picker_code_replace(m_picker.committed);
        else if (!keep) picker_set(m_picker.original);
        picker_change();
        m_picker = ColorPicker{};
    }

    void picker_set(const bark::Rgba& c) {
        if (!m_picker.node) return;
        if (m_picker.code) {
            picker_code_write(c, false);
            return;
        }
        Node& node = mutable_node(*m_picker.node);
        m_picker.last = c;
        const std::optional<bark::Rgba> before = input_color(node);
        if (before && same_color(*before, c)) return;
        set_input_color(document(), node, c);
        if (m_focus == m_picker.node) m_edit = input_text(node);
        fire_value(node, false);
    }

    void picker_apply() {
        picker_set(color_from_hsv(m_picker.hsv, m_picker.show_alpha ? m_picker.alpha : 255));
    }

    void picker_drag(float px, float py) {
        ColorPicker& p = m_picker;
        if (p.drag == PickerPart::Wheel) {
            const float dx = px - p.wheel_cx;
            const float dy = py - p.wheel_cy;
            const double d = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
            p.hsv.h = std::fmod(std::atan2(-static_cast<double>(dy), static_cast<double>(dx)) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
            p.hsv.s = std::clamp(d / std::max(1.0f, p.wheel_r), 0.0, 1.0);
            if (p.hsv.v <= 0.0) p.hsv.v = 1.0;
        } else if (p.drag == PickerPart::Value) {
            p.hsv.v = std::clamp(1.0 - (py - p.value_bar.y) / std::max(1.0f, p.value_bar.h), 0.0, 1.0);
        } else if (p.drag == PickerPart::Alpha) {
            p.alpha = static_cast<std::uint8_t>(std::lround(std::clamp(1.0 - (py - p.alpha_bar.y) / std::max(1.0f, p.alpha_bar.h), 0.0, 1.0) * 255.0));
        } else {
            return;
        }
        picker_apply();
    }

    void picker_press(float px, float py) {
        ColorPicker& p = m_picker;
        const float dx = px - p.wheel_cx;
        const float dy = py - p.wheel_cy;
        if (dx * dx + dy * dy <= (p.wheel_r + 4) * (p.wheel_r + 4)) p.drag = PickerPart::Wheel;
        else if (Rect{ p.value_bar.x - 4, p.value_bar.y - 6, p.value_bar.w + 8, p.value_bar.h + 12 }.contains(px, py)) p.drag = PickerPart::Value;
        else if (p.show_alpha && Rect{ p.alpha_bar.x - 4, p.alpha_bar.y - 6, p.alpha_bar.w + 8, p.alpha_bar.h + 12 }.contains(px, py)) p.drag = PickerPart::Alpha;
        if (p.drag != PickerPart::None) {
            picker_drag(px, py);
            return;
        }
        for (const auto& [rect, format] : p.formats) {
            if (!rect.contains(px, py)) continue;
            if (p.code) {
                p.code_format = format;
                picker_code_write(color_from_hsv(p.hsv, p.show_alpha ? p.alpha : 255), true);
                return;
            }
            Node& node = mutable_node(*p.node);
            node.set_attribute("format", format);
            if (m_focus == p.node) m_edit = input_text(node);
            p.committed = input_text(node);
            document().touch();
            return;
        }
    }

    void layout_picker(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        ColorPicker& p = m_picker;
        if (!p.node) return;
        const ColorSwatch* anchor = nullptr;
        for (const ColorSwatch& s : m_swatches) {
            if (s.node == p.node && s.code == p.code && (!p.code || (s.line == p.code_line && s.begin == p.code_begin))) anchor = &s;
        }
        if (!anchor) {
            p = ColorPicker{};
            return;
        }
        const bark::Rgba current = p.code ? p.last : input_color(*p.node).value_or(bark::Rgba{});
        if (!same_color(current, p.last)) {
            const ColorHsv hsv = color_to_hsv(current);
            if (hsv.s > 0.0 && hsv.v > 0.0) p.hsv.h = hsv.h;
            if (hsv.v > 0.0) p.hsv.s = hsv.s;
            p.hsv.v = hsv.v;
            p.alpha = current.a;
            p.last = current;
        }

        const fizmo::text::TextStyle base = text_style(*p.node);
        const Color fg = raw_text_color(*p.node);
        const Color accent = accent_color(*p.node);
        const float pad = 12.0f;
        const float wheel = 168.0f;
        const float bar = 16.0f;
        fizmo::text::TextStyle mono(12.0, with_alpha(fg, 0.95));
        mono.set_font(fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono));
        fizmo::text::TextStyle label(11.0, with_alpha(fg, 0.55));
        const float row_h = std::max(20.0f, line_height(r, mono) + 6.0f);
        const float panel_w = pad + wheel + 14 + bar + (p.show_alpha ? 10 + bar : 0.0f) + 14 + 56 + pad;
        const float panel_h = pad + wheel + 12 + row_h * 5 + pad - 4;
        float px = anchor->box.x;
        float py = anchor->box.y + anchor->box.h + 4;
        if (py + panel_h > view_h && anchor->box.y - 4 - panel_h >= 0) py = anchor->box.y - 4 - panel_h;
        px = std::clamp(px, 4.0f, std::max(4.0f, view_w - panel_w - 4));
        py = std::clamp(py, 4.0f, std::max(4.0f, view_h - panel_h - 4));
        p.panel = Rect{ px, py, panel_w, panel_h };

        const Color window_bg = to_color(style_of(m_window).color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
        const Color panel_bg = mix(window_bg, fg, 0.06f);
        push_rect(DrawOp::Kind::Fill, px + 2, py + 3, panel_w, panel_h, Color(0, 0, 0, 70), 1, 8);
        push_rect(DrawOp::Kind::Fill, px, py, panel_w, panel_h, panel_bg, 1, 8);
        push_rect(DrawOp::Kind::Stroke, px, py, panel_w, panel_h, with_alpha(fg, 0.22), 1, 8);

        if (!m_color_wheel) {
            m_color_wheel_image = make_color_wheel(256);
            m_color_wheel = std::make_shared<fizmo::graphics::Texture>(std::shared_ptr<const fizmo::images::BitmapImage>(m_color_wheel_image), fizmo::graphics::SampleFilter::Bilinear);
        }
        const float wx = px + pad;
        const float wy = py + pad;
        p.wheel_r = wheel / 2.0f;
        p.wheel_cx = wx + p.wheel_r;
        p.wheel_cy = wy + p.wheel_r;
        DrawOp& disc = push(DrawOp::Kind::Image, wx, wy, wheel, wheel, Color());
        disc.texture = m_color_wheel.get();
        disc.alpha = 1.0f;
        if (p.hsv.v < 1.0) push(DrawOp::Kind::Circle, p.wheel_cx, p.wheel_cy, 0, 0, Color(0, 0, 0, static_cast<std::uint8_t>(std::lround((1.0 - p.hsv.v) * 255.0)))).radius = p.wheel_r;
        const double angle = p.hsv.h * 3.14159265358979323846 / 180.0;
        const float mx = p.wheel_cx + static_cast<float>(std::cos(angle) * p.hsv.s * p.wheel_r);
        const float my = p.wheel_cy - static_cast<float>(std::sin(angle) * p.hsv.s * p.wheel_r);
        DrawOp& ring_dark = push(DrawOp::Kind::CircleStroke, mx, my, 0, 0, Color(0, 0, 0, 200));
        ring_dark.radius = 7;
        ring_dark.stroke = 2;
        DrawOp& ring = push(DrawOp::Kind::CircleStroke, mx, my, 0, 0, Color(255, 255, 255, 255));
        ring.radius = 6;
        ring.stroke = 2;

        auto checker = [&](const Rect& rc) {
            push_rect(DrawOp::Kind::Fill, rc.x, rc.y, rc.w, rc.h, Color(255, 255, 255, 255));
            const float cell = 6.0f;
            for (float cy = 0; cy < rc.h; cy += cell) {
                for (float cx = 0; cx < rc.w; cx += cell) {
                    if ((static_cast<int>(cx / cell) + static_cast<int>(cy / cell)) % 2 == 0) continue;
                    push_rect(DrawOp::Kind::Fill, rc.x + cx, rc.y + cy, std::min(cell, rc.w - cx), std::min(cell, rc.h - cy), Color(200, 200, 200, 255));
                }
            }
        };
        auto marker = [&](const Rect& rc, double t) {
            const float y = rc.y + static_cast<float>((1.0 - t) * rc.h);
            push_rect(DrawOp::Kind::Fill, rc.x - 3, y - 3, rc.w + 6, 6, Color(0, 0, 0, 160), 1, 3);
            push_rect(DrawOp::Kind::Stroke, rc.x - 3, y - 3, rc.w + 6, 6, Color(255, 255, 255, 255), 2, 3);
        };
        const int steps = 48;
        p.value_bar = Rect{ wx + wheel + 14, wy, bar, wheel };
        ColorHsv top = p.hsv;
        top.v = 1.0;
        for (int i = 0; i < steps; ++i) {
            ColorHsv at = top;
            at.v = 1.0 - static_cast<double>(i) / (steps - 1);
            const float sy = p.value_bar.y + p.value_bar.h * static_cast<float>(i) / steps;
            push_rect(DrawOp::Kind::Fill, p.value_bar.x, sy, bar, p.value_bar.h / steps + 1, to_color(color_from_hsv(at)));
        }
        push_rect(DrawOp::Kind::Stroke, p.value_bar.x, p.value_bar.y, bar, wheel, with_alpha(fg, 0.25));
        marker(p.value_bar, p.hsv.v);
        float right = p.value_bar.x + bar;
        const bark::Rgba solid = color_from_hsv(p.hsv);
        if (p.show_alpha) {
            p.alpha_bar = Rect{ right + 10, wy, bar, wheel };
            checker(p.alpha_bar);
            for (int i = 0; i < steps; ++i) {
                const float sy = p.alpha_bar.y + p.alpha_bar.h * static_cast<float>(i) / steps;
                const double a = 1.0 - static_cast<double>(i) / (steps - 1);
                push_rect(DrawOp::Kind::Fill, p.alpha_bar.x, sy, bar, p.alpha_bar.h / steps + 1, Color(solid.r, solid.g, solid.b, static_cast<std::uint8_t>(std::lround(a * 255.0))));
            }
            push_rect(DrawOp::Kind::Stroke, p.alpha_bar.x, p.alpha_bar.y, bar, wheel, with_alpha(fg, 0.25));
            marker(p.alpha_bar, p.alpha / 255.0);
            right = p.alpha_bar.x + bar;
        }

        const float sx = right + 14;
        const bark::Rgba now = color_from_hsv(p.hsv, p.show_alpha ? p.alpha : 255);
        const float lh_label = line_height(r, label);
        push_text(sx, wy, 56, lh_label, "New", label);
        const Rect fresh{ sx, wy + lh_label + 4, 56, 40 };
        checker(fresh);
        push_rect(DrawOp::Kind::Fill, fresh.x, fresh.y, fresh.w, fresh.h, to_color(now));
        push_rect(DrawOp::Kind::Stroke, fresh.x, fresh.y, fresh.w, fresh.h, with_alpha(fg, 0.25));
        push_text(sx, fresh.y + fresh.h + 10, 56, lh_label, "Before", label);
        const Rect old{ sx, fresh.y + fresh.h + 14 + lh_label, 56, 40 };
        checker(old);
        push_rect(DrawOp::Kind::Fill, old.x, old.y, old.w, old.h, to_color(p.original));
        push_rect(DrawOp::Kind::Stroke, old.x, old.y, old.w, old.h, with_alpha(fg, 0.25));

        p.formats.clear();
        const std::string chosen = p.code ? p.code_format : p.node->attribute_or("format", "hex");
        const std::vector<std::pair<std::string, std::string>> rows = { { "hex", "HEX" }, { "rgb", "RGB" }, { "rgba", "RGBA" }, { "hsl", "HSL" }, { "hsla", "HSLA" } };
        float ry = wy + wheel + 12;
        for (const auto& [format, name] : rows) {
            const Rect row{ px + 6, ry, panel_w - 12, row_h };
            if (format == chosen) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(accent, 0.22), 1, 4);
            else if (row.contains(m_mouse_x, m_mouse_y)) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(fg, 0.07), 1, 4);
            const float ty = row.y + (row_h - line_height(r, mono)) / 2.0f;
            push_text(row.x + 8, row.y + (row_h - line_height(r, label)) / 2.0f, 44, line_height(r, label), name, label);
            push_text(row.x + 56, ty, row.w - 60, line_height(r, mono), format == "hex" ? format_color(now) : format_color(now, format), mono);
            p.formats.emplace_back(row, format);
            ry += row_h;
        }
        (void)base;
    }

    void mouse_down(float px, float py) {
        if (code_hover_click(px, py)) return;
        hide_code_hover();
        if (m_picker.node) {
            if (m_picker.panel.contains(px, py)) {
                picker_press(px, py);
                return;
            }
            const Node* was = m_picker.node;
            const bool was_code = m_picker.code;
            const std::size_t was_line = m_picker.code_line;
            const std::size_t was_begin = m_picker.code_begin;
            close_picker(true);
            const ColorSwatch* again = swatch_at(px, py);
            if (again && again->node == was && (!was_code || (again->line == was_line && again->begin == was_begin))) return;
        }
        if (const ColorSwatch* s = swatch_at(px, py)) {
            if (s->code) {
                focus_code(s->node);
                open_code_picker(*s);
                return;
            }
            blur(true);
            blur_code();
            set_focused(s->node);
            open_picker(s->node);
            return;
        }
        const Node* target = hit_element(px, py);
        if (m_modal && !m_modal_panel.contains(px, py)) {
            Event down = pointer_event("mouse_down", px, py, 1);
            if (dispatch(m_modal, down) && !m_modal->has_attribute("persistent")) close_modal(document(), mutable_node(*m_modal));
            return;
        }
        m_pressed = target;
        update_chain(m_active, target, StateActive, nullptr, nullptr);
        Event down = pointer_event("mouse_down", px, py, 1);
        const bool allowed = dispatch(target, down);

        if (allowed) {
            for (std::size_t hi = live_hits(); hi < m_hits.size(); ++hi) {
                const Hit& hit = m_hits[hi];
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
                    case InputKind::Code: {
                        focus_code(hit.node);
                        CodeEditor& ed = m_code[hit.node];
                        const CodePos at = code_pos_at(ed, px, py);
                        const bool shift = (m_mods & ModifierShift) != 0;
                        const bool alt = (m_mods & ModifierAlt) != 0;
                        ed.column_mode = false;
                        ed.reveal = true;
                        const bool ctrl = (m_mods & (ModifierControl | ModifierMeta)) != 0;
                        if (ctrl && !alt && !shift) {
                            const std::optional<CodePos> on = code_char_at(ed, px, py);
                            ed.buffer.set_caret(on.value_or(at));
                            Event go;
                            go.type = "goto";
                            const CodePos where = on.value_or(at);
                            go.x = static_cast<double>(where.line + 1);
                            go.y = static_cast<double>(code_display_column(ed.buffer.line(where.line), where.column, ed.tab_size) + 1);
                            dispatch(hit.node, go);
                            return;
                        }
                        if (alt && !shift) {
                            ed.buffer.toggle_cursor(at);
                            return;
                        }
                        if (shift && !ed.buffer.has_selection() && !ed.buffer.has_extra_cursors()) {
                            const CodePos from = ed.buffer.caret();
                            const float cell = std::max(1.0f, ed.char_w);
                            const long clicked = std::lround((px - ed.text_x + ed.scroll_x) / cell);
                            const long column = static_cast<long>(code_display_column(ed.buffer.line(from.line), from.column, ed.tab_size));
                            if (at.line != from.line && clicked == column) {
                                ed.buffer.column_select(from, at.line);
                                ed.column_mode = true;
                                ed.column_anchor = from;
                                ed.column_head = at.line;
                                return;
                            }
                        }
                        ed.buffer.set_caret(at, shift);
                        m_code_drag = hit.node;
                        return;
                    }
                    case InputKind::File:
                        blur(true);
                        blur_code();
                        set_focused(hit.node);
                        return;
                    case InputKind::Slider:
                        blur(true);
                        blur_code();
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
        blur_code();
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
        if (button == 1 && m_picker.drag != PickerPart::None) {
            m_picker.drag = PickerPart::None;
            picker_change();
            return;
        }
        const Node* target = hit_element(px, py);
        Event up = pointer_event("mouse_up", px, py, button);
        dispatch(target, up);
        if (button != 1) return;
        m_code_drag = nullptr;
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
        if (!dispatch(common, click)) return;
        follow_link(common);
        for (const Node* n = common; n && n != &m_window; n = n->parent()) {
            if (input_kind_of(*n) == InputKind::File) {
                open_dialog(n);
                break;
            }
        }
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
        if (m_code_focus) {
            if (code_read_only(*m_code_focus)) return;
            std::string clean;
            for (char c : text) {
                if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) clean += c;
            }
            if (clean.empty()) return;
            CodeEditor& ed = m_code[m_code_focus];
            ed.column_mode = false;
            ed.buffer.insert(clean);
            code_changed(m_code_focus, clean);
            return;
        }
        if (!m_focus) return;
        bool changed = false;
        for (const std::string& cp : split_code_points(text)) {
            if (m_edit_all && input_accepts(*m_focus, "", cp)) {
                m_edit.clear();
                m_edit_all = false;
            }
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
        for (std::size_t hi = live_hits(); hi < m_hits.size(); ++hi) {
            if (is_text_input(m_hits[hi].kind)) order.push_back(m_hits[hi].node);
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
        if (m_picker.node && !m_focus && (key == Key::Enter || key == Key::Escape)) {
            close_picker(key == Key::Enter);
            return;
        }
        if (m_modal && key == Key::Escape && !m_focus && !m_code_focus && !m_modal->has_attribute("persistent")) {
            close_modal(document(), mutable_node(*m_modal));
            return;
        }
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
        if (m_code_focus) {
            code_key(key, e.mods);
            return;
        }
        if (!m_focus) {
            if (m_focused && (key == Key::Enter || key == Key::Space) && (m_focused->tag() == "button" || input_kind_of(*m_focused) == InputKind::File) && !is_disabled(m_focused)) {
                Event click;
                click.type = "click";
                const Node* clicked = m_focused;
                if (dispatch(clicked, click)) {
                    if (input_kind_of(*clicked) == InputKind::File) open_dialog(clicked);
                    else follow_link(clicked);
                }
            }
            return;
        }
        Node& node = mutable_node(*m_focus);
        const bool select_all = key == Key::A && (fizmo::input::has(e.mods, fizmo::input::Modifiers::Control) || fizmo::input::has(e.mods, fizmo::input::Modifiers::Meta));
        if (select_all) {
            m_edit_all = !m_edit.empty();
            return;
        }
        const bool was_all = m_edit_all;
        if (key == Key::Backspace || key == Key::Delete || key == Key::Up || key == Key::Down || key == Key::Left || key == Key::Right || key == Key::Home || key == Key::End) m_edit_all = false;
        switch (key) {
            case Key::Delete:
            case Key::Backspace:
                if (key == Key::Delete && !was_all) break;
                if (was_all) m_edit.clear();
                if (!m_edit.empty() || was_all) {
                    if (!was_all) pop_code_point(m_edit);
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

    bool scroll_inner(float px, float py, float amount, bool horizontal) {
        for (const Scroller& sc : m_scrollers) {
            if (!sc.rect.contains(px, py)) continue;
            if (sc.code) {
                CodeEditor& ed = m_code[sc.node];
                if (horizontal) ed.scroll_x = std::max(0.0f, ed.scroll_x - amount);
                else {
                    if (sc.max <= 0.0f) continue;
                    ed.scroll_y = std::clamp(ed.scroll_y - amount, 0.0f, sc.max);
                }
                return true;
            }
            if (horizontal || sc.max <= 0.0f) continue;
            float& s = m_elem_scroll[sc.node];
            const float before = s;
            s = std::clamp(s - amount, 0.0f, sc.max);
            if (s != before || amount != 0.0f) return true;
        }
        return false;
    }

    bool code_read_only(const Node& node) const { return node.has_attribute("readonly") || is_disabled(&node); }

    std::optional<CodePos> code_char_at(const CodeEditor& ed, float px, float py) const {
        if (py < ed.text_y || px < ed.text_x || px > ed.text_x + ed.view_w || py > ed.text_y + ed.view_h) return std::nullopt;
        const float fy = (py - ed.text_y + ed.scroll_y) / std::max(1.0f, ed.line_h);
        if (fy < 0.0f) return std::nullopt;
        const std::size_t line = static_cast<std::size_t>(fy);
        if (line >= ed.buffer.line_count()) return std::nullopt;
        const std::string& text = ed.buffer.line(line);
        const float fx = (px - ed.text_x + ed.scroll_x) / std::max(1.0f, ed.char_w);
        if (fx < 0.0f) return std::nullopt;
        const std::size_t display = static_cast<std::size_t>(fx);
        if (display >= code_display_column(text, text.size(), ed.tab_size)) return std::nullopt;
        std::size_t byte = code_byte_column(text, display, ed.tab_size);
        if (byte > 0 && code_display_column(text, byte, ed.tab_size) > display) --byte;
        if (byte >= text.size()) return std::nullopt;
        return CodePos{ line, byte };
    }

    std::optional<std::pair<std::size_t, std::size_t>> code_link_span(const Node& element, CodeEditor& ed, const CodePos& at) {
        const std::string attr = element.attribute_or("links", "");
        if (attr != ed.links_attr) {
            ed.links_attr = attr;
            ed.links.set_spans(attr);
        }
        if (at.line >= ed.buffer.line_count()) return std::nullopt;
        const std::string& text = ed.buffer.line(at.line);
        for (const CodeToken& t : ed.links.spans(at.line, text)) {
            if (t.begin <= at.column && at.column < t.end) return std::make_pair(t.begin, t.end);
        }
        if (element.attribute_or("link-words", "false") != "true" || at.column >= text.size()) return std::nullopt;
        auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; };
        if (!word(text[at.column])) return std::nullopt;
        std::size_t b = at.column;
        std::size_t e = at.column;
        while (b > 0 && word(text[b - 1])) --b;
        while (e < text.size() && word(text[e])) ++e;
        return std::make_pair(b, e);
    }

    const Node* code_node_at(float px, float py) const {
        if (m_modal && !m_modal_panel.contains(px, py)) return nullptr;
        for (std::size_t i = m_hits.size(); i > 0; --i) {
            const Hit& hit = m_hits[i - 1];
            if (hit.kind == InputKind::Code && hit.contains(px, py)) return hit.node;
            if (hit.contains(px, py)) return nullptr;
        }
        return nullptr;
    }

    void hide_code_hover() {
        CodeHover& h = m_code_hover;
        if (h.timer) document().events().clear_timer(h.timer);
        h.timer = 0;
        h.requested = false;
        h.shown = false;
        h.pending = nullptr;
        h.actions.clear();
    }

    void code_hover_move(float px, float py) {
        CodeHover& h = m_code_hover;
        if (h.shown) {
            const float left = std::min(h.anchor.x, h.panel.x) - 4.0f;
            const float top = std::min(h.anchor.y, h.panel.y) - 4.0f;
            const float right = std::max(h.anchor.x + h.anchor.w, h.panel.x + h.panel.w) + 4.0f;
            const float bottom = std::max(h.anchor.y + h.anchor.h, h.panel.y + h.panel.h) + 4.0f;
            const bool in_panel = Rect{ h.panel.x - 6, h.panel.y - 6, h.panel.w + 12, h.panel.h + 12 }.contains(px, py);
            const bool between = Rect{ left, top, right - left, bottom - top }.contains(px, py) && px >= h.anchor.x - 4.0f && px <= h.anchor.x + std::max(h.anchor.w, 24.0f) + 4.0f;
            if (in_panel || h.anchor.contains(px, py) || between) return;
        }
        const Node* node = (m_code_drag || m_picker.node || m_pressed) ? nullptr : code_node_at(px, py);
        std::optional<CodePos> at;
        if (node) at = code_char_at(m_code[node], px, py);
        if (h.requested && (!at || node != h.node || at->line != h.pos.line || !h.shown)) {
            const bool same = at && node == h.node && *at == h.pos;
            if (!same) hide_code_hover();
        }
        if (!at) {
            if (h.timer) document().events().clear_timer(h.timer);
            h.timer = 0;
            h.pending = nullptr;
            return;
        }
        if (h.requested && node == h.node && *at == h.pos) return;
        if (h.pending == node && h.pending_pos == *at && h.timer) return;
        if (h.timer) document().events().clear_timer(h.timer);
        h.pending = node;
        h.pending_pos = *at;
        std::weak_ptr<int> alive = m_alive;
        h.timer = document().events().set_timeout(450.0, [this, alive] {
            if (alive.expired()) return;
            code_hover_fire();
        });
    }

    void code_hover_fire() {
        CodeHover& h = m_code_hover;
        h.timer = 0;
        const Node* node = h.pending;
        if (!node || !document().is_attached(*node) || m_code_drag || m_picker.node) return;
        if (code_node_at(m_mouse_x, m_mouse_y) != node) return;
        CodeEditor& ed = m_code[node];
        const std::optional<CodePos> at = code_char_at(ed, m_mouse_x, m_mouse_y);
        if (!at || *at != h.pending_pos) return;
        mutable_node(*node).remove_attribute("hover");
        h.node = node;
        h.pos = *at;
        h.requested = true;
        h.shown = false;
        Event e;
        e.type = "hover";
        e.x = static_cast<double>(at->line + 1);
        e.y = static_cast<double>(code_display_column(ed.buffer.line(at->line), at->column, ed.tab_size) + 1);
        dispatch(node, e);
        poke();
    }

    struct HoverPiece {
        std::string text;
        int kind = 0;
        std::string action;
    };

    std::vector<HoverPiece> hover_pieces(const std::string& line) {
        std::vector<HoverPiece> out;
        std::string run;
        auto flush = [&] {
            if (!run.empty()) out.push_back(HoverPiece{ run, 0, std::string() });
            run.clear();
        };
        std::size_t i = 0;
        while (i < line.size()) {
            if (line.compare(i, 2, "**") == 0) {
                const std::size_t close = line.find("**", i + 2);
                if (close != std::string::npos) {
                    flush();
                    out.push_back(HoverPiece{ line.substr(i + 2, close - i - 2), 1, std::string() });
                    i = close + 2;
                    continue;
                }
            }
            if (line[i] == '`') {
                const std::size_t close = line.find('`', i + 1);
                if (close != std::string::npos) {
                    flush();
                    out.push_back(HoverPiece{ line.substr(i + 1, close - i - 1), 2, std::string() });
                    i = close + 1;
                    continue;
                }
            }
            if (line[i] == '[') {
                const std::size_t mid = line.find("](", i + 1);
                const std::size_t close = mid == std::string::npos ? std::string::npos : line.find(')', mid + 2);
                if (close != std::string::npos) {
                    flush();
                    const std::string action = line.substr(mid + 2, close - mid - 2);
                    out.push_back(HoverPiece{ line.substr(i + 1, mid - i - 1), action.rfind("color:", 0) == 0 ? 4 : 3, action });
                    i = close + 1;
                    continue;
                }
            }
            run += line[i++];
        }
        flush();
        return out;
    }

    void layout_code_hover(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        CodeHover& h = m_code_hover;
        h.shown = false;
        h.actions.clear();
        if (!h.requested || !h.node || !document().is_attached(*h.node)) return;
        const auto found = m_code.find(h.node);
        if (found == m_code.end()) return;
        CodeEditor& ed = found->second;
        const std::string attr = h.node->attribute_or("hover", "");
        const std::size_t nl = attr.find('\n');
        if (attr.empty() || nl == std::string::npos) return;
        std::size_t hl = 0;
        std::size_t hc = 0;
        std::size_t hn = 0;
        {
            std::istringstream head(attr.substr(0, nl));
            if (!(head >> hl >> hc >> hn)) return;
        }
        if (hl != h.pos.line || hl >= ed.buffer.line_count()) return;
        if (hn > 0 && (h.pos.column < hc || h.pos.column >= hc + hn)) return;
        if (hn == 0) {
            hc = h.pos.column;
            hn = 1;
        }
        const std::string body = attr.substr(nl + 1);
        if (body.find_first_not_of(" \n\t") == std::string::npos) return;
        const std::string& row = ed.buffer.line(hl);
        const std::size_t end = std::min(row.size(), hc + hn);
        h.anchor = Rect{ ed.text_x + static_cast<float>(code_display_column(row, hc, ed.tab_size)) * ed.char_w - ed.scroll_x, ed.text_y + static_cast<float>(hl) * ed.line_h - ed.scroll_y,
                         static_cast<float>(code_display_column(row, end, ed.tab_size) - code_display_column(row, std::min(hc, row.size()), ed.tab_size)) * ed.char_w, ed.line_h };
        if (h.anchor.y + h.anchor.h < ed.text_y || h.anchor.y > ed.text_y + ed.view_h) return;

        const bark::ComputedStyle& style = style_of(*h.node);
        const Color fg = raw_text_color(*h.node);
        const Color accent = accent_color(*h.node);
        const Color editor_bg = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
        const std::optional<bark::Rgba> hover_bg = style.color("code-hover");
        const Color panel_bg = hover_bg ? to_color(*hover_bg) : mix(editor_bg, fg, 0.06f);
        fizmo::text::TextStyle mono = text_style(*h.node);
        if (!style.get("font-family")) mono.set_font(fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono));
        mono.set_size(std::max(10.0, mono.size() * 0.92));
        mono.set_bold(false);
        mono.set_italic(false);
        mono.set_underline(false);
        mono.set_fg_color(fg);
        fizmo::text::TextStyle prose(std::max(11.0, mono.size() * 0.95), fg);
        fizmo::text::TextStyle strong = prose;
        strong.set_bold(true);
        fizmo::text::TextStyle link = prose;
        link.set_fg_color(accent);
        const std::vector<CodeTokenLook> looks = code_token_looks(style);
        const CodeLanguage editor_lang = code_language_from_name(h.node->attribute_or("language", "plain"));

        struct Item {
            int kind = 0;
            float x = 0;
            float y = 0;
            float w = 0;
            float h = 0;
            std::string text;
            fizmo::text::TextStyle style;
            Color color;
            std::string action;
        };
        std::vector<Item> items;
        const float pad = 8.0f;
        const float max_w = std::min(620.0f, std::max(160.0f, view_w - 24.0f));
        const float mono_h = line_height(r, mono);
        const float prose_h = line_height(r, prose) + 2.0f;
        float y = 0;
        float widest = 0;
        bool in_code = false;
        CodeLanguage code_lang = editor_lang;
        CodeLineState state;
        bool first_block = true;
        std::istringstream lines(body);
        std::string text;
        while (std::getline(lines, text)) {
            if (!text.empty() && text.back() == '\r') text.pop_back();
            if (text.rfind("```", 0) == 0) {
                if (!in_code) {
                    const std::string name = text.substr(3);
                    code_lang = name.empty() ? editor_lang : code_language_from_name(name);
                    state = CodeLineState{};
                    if (!first_block) y += 4.0f;
                } else {
                    y += 4.0f;
                }
                in_code = !in_code;
                first_block = false;
                continue;
            }
            first_block = false;
            if (in_code) {
                const std::string shown = code_expand_tabs(text, 4);
                float x = 0;
                for (const CodeToken& t : highlight_code_line(code_lang, shown, state)) {
                    const std::string piece = shown.substr(t.begin, t.end - t.begin);
                    const float w = text_width(r, piece, mono);
                    if (x + w > max_w) break;
                    Item it;
                    it.kind = 0;
                    it.x = x;
                    it.y = y;
                    it.w = w;
                    it.h = mono_h;
                    it.text = piece;
                    it.style = mono;
                    const CodeTokenLook& look = looks[static_cast<std::size_t>(t.kind)];
                    if (look.bold) it.style.set_bold(true);
                    if (look.italic) it.style.set_italic(true);
                    it.style.set_fg_color(look.color.value_or(fg));
                    items.push_back(it);
                    x += w;
                }
                widest = std::max(widest, x);
                y += mono_h;
                continue;
            }
            if (text == "---") {
                Item it;
                it.kind = 5;
                it.y = y + 4.0f;
                it.h = 1.0f;
                items.push_back(it);
                y += 9.0f;
                continue;
            }
            float x = 0;
            for (const HoverPiece& piece : hover_pieces(text)) {
                if (piece.kind == 4) {
                    const float size = prose_h - 6.0f;
                    if (x + size > max_w) {
                        x = 0;
                        y += prose_h;
                    }
                    Item it;
                    it.kind = 4;
                    it.x = x;
                    it.y = y + 3.0f;
                    it.w = size;
                    it.h = size;
                    it.color = to_color(parse_color_text(piece.action.substr(6)).value_or(bark::Rgba{}));
                    items.push_back(it);
                    x += size + 4.0f;
                    continue;
                }
                fizmo::text::TextStyle st = piece.kind == 1 ? strong : piece.kind == 2 ? mono : piece.kind == 3 ? link : prose;
                std::string word;
                std::vector<std::string> words;
                for (char c : piece.text) {
                    word += c;
                    if (c == ' ') {
                        words.push_back(word);
                        word.clear();
                    }
                }
                if (!word.empty()) words.push_back(word);
                for (const std::string& w : words) {
                    const float ww = text_width(r, w, st);
                    if (x > 0 && x + ww > max_w) {
                        widest = std::max(widest, x);
                        x = 0;
                        y += prose_h;
                    }
                    Item it;
                    it.kind = piece.kind == 2 ? 2 : piece.kind == 3 ? 3 : 1;
                    it.x = x;
                    it.y = y;
                    it.w = ww;
                    it.h = prose_h;
                    it.text = w;
                    it.style = st;
                    it.action = piece.action;
                    items.push_back(it);
                    x += ww;
                }
            }
            widest = std::max(widest, x);
            y += prose_h;
        }
        if (items.empty()) return;
        const float panel_w = std::min(max_w, widest) + pad * 2.0f;
        const float panel_h = std::min(view_h * 0.6f, y + pad * 2.0f);
        float px = std::clamp(h.anchor.x, 4.0f, std::max(4.0f, view_w - panel_w - 4.0f));
        float py = h.anchor.y - 4.0f - panel_h;
        if (py < std::max(4.0f, ed.text_y - 4.0f)) py = h.anchor.y + h.anchor.h + 4.0f;
        py = std::clamp(py, 4.0f, std::max(4.0f, view_h - panel_h - 4.0f));
        h.panel = Rect{ px, py, panel_w, panel_h };
        push_rect(DrawOp::Kind::Fill, px + 2, py + 3, panel_w, panel_h, Color(0, 0, 0, 70), 1, 6);
        push_rect(DrawOp::Kind::Fill, px, py, panel_w, panel_h, panel_bg, 1, 6);
        push_rect(DrawOp::Kind::Stroke, px, py, panel_w, panel_h, with_alpha(fg, 0.22), 1, 6);
        push_clip(Rect{ px + 1, py + 1, panel_w - 2, panel_h - 2 });
        for (Item& it : items) {
            const float ix = px + pad + it.x;
            const float iy = py + pad + it.y;
            if (it.kind == 5) {
                push_rect(DrawOp::Kind::Fill, px + 1, iy, panel_w - 2, 1, with_alpha(fg, 0.15));
                continue;
            }
            if (it.kind == 4) {
                push_rect(DrawOp::Kind::Fill, ix, iy, it.w, it.h, it.color, 1, 2);
                push_rect(DrawOp::Kind::Stroke, ix, iy, it.w, it.h, with_alpha(fg, 0.45), 1, 2);
                continue;
            }
            const float ty = iy + (it.h - line_height(r, it.style)) / 2.0f;
            if (it.kind == 2 && it.text.find_first_not_of(' ') != std::string::npos) push_rect(DrawOp::Kind::Fill, ix - 1, iy + 1, it.w + 2, it.h - 2, with_alpha(fg, 0.08), 1, 3);
            if (it.kind == 3) {
                const Rect hit{ ix, iy, it.w, it.h };
                if (hit.contains(m_mouse_x, m_mouse_y)) it.style.set_underline(true);
                h.actions.emplace_back(hit, it.action);
            }
            push_text(ix, ty, it.w + 4.0f, it.h, it.text, it.style);
        }
        pop_clip();
        h.shown = true;
    }

    bool code_hover_click(float px, float py) {
        CodeHover& h = m_code_hover;
        if (!h.shown || !h.panel.contains(px, py)) return false;
        for (const auto& [rect, action] : h.actions) {
            if (!rect.contains(px, py) || action.empty()) continue;
            Event e;
            e.type = "hover_action";
            e.key = action;
            e.x = static_cast<double>(h.pos.line + 1);
            e.y = static_cast<double>(h.pos.column + 1);
            dispatch(h.node, e);
            break;
        }
        return true;
    }

    void open_code_picker(const ColorSwatch& sw) {
        const auto found = m_code.find(sw.node);
        if (found == m_code.end() || sw.line >= found->second.buffer.line_count()) return;
        const std::string& text = found->second.buffer.line(sw.line);
        const std::optional<bark::Rgba> c = parse_color_text(std::string_view(text).substr(sw.begin, sw.end - sw.begin));
        if (!c) return;
        m_picker = ColorPicker{};
        m_picker.node = sw.node;
        m_picker.hsv = color_to_hsv(*c);
        m_picker.alpha = c->a;
        m_picker.original = *c;
        m_picker.last = *c;
        m_picker.code = true;
        m_picker.code_line = sw.line;
        m_picker.code_begin = sw.begin;
        m_picker.code_end = sw.end;
        m_picker.code_format = sw.format;
        m_picker.committed = text.substr(sw.begin, sw.end - sw.begin);
    }

    void picker_code_write(const bark::Rgba& c, bool force) {
        ColorPicker& p = m_picker;
        const auto found = m_code.find(p.node);
        if (found == m_code.end() || code_read_only(*p.node)) return;
        CodeEditor& ed = found->second;
        if (p.code_line >= ed.buffer.line_count()) return;
        if (!force && same_color(p.last, c)) return;
        p.last = c;
        std::string literal = p.code_format == "hex" ? format_color(c) : format_color(c, p.code_format);
        const std::string& row = ed.buffer.line(p.code_line);
        const std::string old = row.substr(std::min(p.code_begin, row.size()), p.code_end - std::min(p.code_begin, p.code_end));
        if (p.code_format == "hex" && !old.empty() && std::any_of(old.begin(), old.end(), [](char ch) { return ch >= 'A' && ch <= 'F'; })) {
            for (char& ch : literal) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        picker_code_replace(literal);
    }

    void picker_code_replace(const std::string& literal) {
        ColorPicker& p = m_picker;
        const auto found = m_code.find(p.node);
        if (found == m_code.end() || code_read_only(*p.node)) return;
        CodeEditor& ed = found->second;
        if (p.code_line >= ed.buffer.line_count()) return;
        const std::string& row = ed.buffer.line(p.code_line);
        const std::string old = row.substr(std::min(p.code_begin, row.size()), p.code_end - std::min(p.code_begin, p.code_end));
        if (literal == old) return;
        const CodePos keep = ed.buffer.caret();
        ed.buffer.select(CodePos{ p.code_line, p.code_begin }, CodePos{ p.code_line, p.code_end });
        ed.buffer.insert(literal);
        p.code_end = p.code_begin + literal.size();
        ed.buffer.set_caret(keep.line == p.code_line && keep.column > p.code_begin ? CodePos{ keep.line, p.code_end } : keep);
        code_changed(p.node);
    }

    CodePos code_pos_at(const CodeEditor& ed, float px, float py) const {
        const float fy = (py - ed.text_y + ed.scroll_y) / std::max(1.0f, ed.line_h);
        std::size_t line = fy <= 0.0f ? 0 : static_cast<std::size_t>(fy);
        if (line >= ed.buffer.line_count()) {
            line = ed.buffer.line_count() - 1;
            return CodePos{ line, ed.buffer.line(line).size() };
        }
        const float fx = (px - ed.text_x + ed.scroll_x) / std::max(1.0f, ed.char_w);
        const std::size_t col = fx <= 0.0f ? 0 : static_cast<std::size_t>(std::lround(fx));
        return CodePos{ line, code_byte_column(ed.buffer.line(line), col, ed.tab_size) };
    }

    void focus_code(const Node* node) {
        if (m_code_focus == node) return;
        blur(true);
        blur_code();
        m_code_focus = node;
        CodeEditor& ed = m_code[node];
        ed.focus_start = ed.buffer.text();
        ed.last_reported = CodePos{ static_cast<std::size_t>(-1), 0 };
        if (m_native) m_native->start_text_input();
        set_focused(node);
    }

    void blur_code() {
        if (!m_code_focus) return;
        const Node* node = m_code_focus;
        m_code_focus = nullptr;
        m_code_drag = nullptr;
        if (m_native && !m_focus) m_native->stop_text_input();
        CodeEditor& ed = m_code[node];
        if (document().is_attached(*node) && ed.buffer.text() != ed.focus_start) {
            ed.focus_start = ed.buffer.text();
            Event change;
            change.type = "change";
            dispatch(node, change);
        }
    }

    void code_changed(const Node* node, const std::string& typed = std::string()) {
        CodeEditor& ed = m_code[node];
        ed.synced = ed.buffer.text();
        mutable_node(*node).set_attribute("value", ed.synced);
        document().touch();
        ed.reveal = true;
        Event input;
        input.type = "input";
        input.text = typed;
        dispatch(node, input);
    }

    void code_drag_to(float px, float py) {
        if (!m_code_drag || !document().is_attached(*m_code_drag)) {
            m_code_drag = nullptr;
            return;
        }
        CodeEditor& ed = m_code[m_code_drag];
        ed.buffer.set_caret(code_pos_at(ed, px, py), true);
        ed.reveal = true;
    }

    void code_double_click(float px, float py) {
        for (const Hit& hit : m_hits) {
            if (hit.kind != InputKind::Code || !hit.contains(px, py)) continue;
            focus_code(hit.node);
            CodeEditor& ed = m_code[hit.node];
            ed.buffer.select_word_at(code_pos_at(ed, px, py));
            m_code_drag = nullptr;
            return;
        }
    }

    void report_cursors() {
        if (!m_code_focus || !document().is_attached(*m_code_focus)) return;
        CodeEditor& ed = m_code[m_code_focus];
        const CodePos c = ed.buffer.caret();
        if (c == ed.last_reported) return;
        ed.last_reported = c;
        Event e;
        e.type = "cursor";
        e.bubbles = true;
        e.x = static_cast<double>(c.line + 1);
        e.y = static_cast<double>(code_display_column(ed.buffer.line(c.line), c.column, ed.tab_size) + 1);
        dispatch(m_code_focus, e);
    }

    void code_key(fizmo::input::Key key, fizmo::input::Modifiers mods) {
        using fizmo::input::Key;
        using fizmo::input::Modifiers;
        const Node* node = m_code_focus;
        CodeEditor& ed = m_code[node];
        CodeBuffer& buf = ed.buffer;
        const bool shift = fizmo::input::has(mods, Modifiers::Shift);
        const bool ctrl = fizmo::input::has(mods, Modifiers::Control) || fizmo::input::has(mods, Modifiers::Meta);
        const bool ro = code_read_only(*node);
        const bool spaces = node->attribute_or("indent", "spaces") != "tabs";
        const std::size_t page = static_cast<std::size_t>(std::max(1.0f, ed.view_h / std::max(1.0f, ed.line_h) - 1.0f));
        bool edited = false;
        const bool vertical = key == Key::Up || key == Key::Down;
        if (vertical && shift && !ctrl && (ed.column_mode || (!buf.any_selection() && !buf.has_extra_cursors()))) {
            if (!ed.column_mode) {
                ed.column_mode = true;
                ed.column_anchor = buf.caret();
                ed.column_head = buf.caret().line;
            }
            if (key == Key::Up && ed.column_head > 0) --ed.column_head;
            if (key == Key::Down && ed.column_head + 1 < buf.line_count()) ++ed.column_head;
            buf.column_select(ed.column_anchor, ed.column_head);
            ed.reveal = true;
            return;
        }
        if (key != Key::LeftShift && key != Key::RightShift) ed.column_mode = false;
        switch (key) {
            case Key::Left: buf.move_left(shift, ctrl); break;
            case Key::Right: buf.move_right(shift, ctrl); break;
            case Key::Up:
                if (ctrl) ed.scroll_y = std::max(0.0f, ed.scroll_y - ed.line_h);
                else buf.move_up(shift);
                break;
            case Key::Down:
                if (ctrl) ed.scroll_y += ed.line_h;
                else buf.move_down(shift);
                break;
            case Key::Home: ctrl ? buf.move_start(shift) : buf.move_home(shift); break;
            case Key::End: ctrl ? buf.move_finish(shift) : buf.move_end(shift); break;
            case Key::PageUp: buf.move_up(shift, page); break;
            case Key::PageDown: buf.move_down(shift, page); break;
            case Key::Backspace: if (!ro) { buf.backspace(ctrl); edited = true; } break;
            case Key::Delete: if (!ro) { buf.delete_forward(ctrl); edited = true; } break;
            case Key::Enter:
            case Key::NumpadEnter: if (!ro) { buf.new_line(ed.tab_size, spaces); edited = true; } break;
            case Key::Tab:
                if (!ro) {
                    if (shift) buf.outdent(ed.tab_size);
                    else buf.indent(ed.tab_size, spaces);
                    edited = true;
                }
                break;
            case Key::Escape:
                if (buf.has_extra_cursors()) buf.clear_extra_cursors();
                else buf.set_caret(buf.caret());
                break;
            case Key::A: if (ctrl) buf.select_all(); break;
            case Key::C:
                if (ctrl && m_native) {
                    if (buf.has_extra_cursors()) {
                        if (buf.any_selection()) m_native->set_clipboard_text(buf.selected_texts());
                    } else {
                        m_native->set_clipboard_text(buf.has_selection() ? buf.selected_text() : buf.line(buf.caret().line) + "\n");
                    }
                }
                break;
            case Key::X:
                if (ctrl && m_native && !ro) {
                    if (buf.has_extra_cursors()) {
                        if (buf.any_selection()) {
                            m_native->set_clipboard_text(buf.selected_texts());
                            buf.delete_selection();
                            edited = true;
                        }
                        break;
                    }
                    if (!buf.has_selection()) buf.select_line(buf.caret().line);
                    m_native->set_clipboard_text(buf.selected_text());
                    buf.delete_selection();
                    edited = true;
                }
                break;
            case Key::V:
                if (ctrl && m_native && !ro) {
                    const std::string text = m_native->clipboard_text();
                    if (!text.empty()) {
                        if (buf.has_extra_cursors()) {
                            std::vector<std::string> pieces(1);
                            for (char c : text) {
                                if (c == '\r') continue;
                                if (c == '\n') pieces.emplace_back();
                                else pieces.back() += c;
                            }
                            if (pieces.size() > 1 && pieces.back().empty()) pieces.pop_back();
                            if (pieces.size() == buf.cursor_count()) buf.insert_each(pieces);
                            else buf.insert(text);
                        } else {
                            buf.insert(text);
                        }
                        edited = true;
                    }
                }
                break;
            case Key::Z:
                if (ctrl && !ro) edited = shift ? buf.redo() : buf.undo();
                break;
            case Key::Y:
                if (ctrl && !ro) edited = buf.redo();
                break;
            default: return;
        }
        ed.reveal = true;
        if (edited) code_changed(node);
    }

    void open_dialog(const Node* node) {
        if (!node || m_dialogs_pending.count(node) || is_disabled(node)) return;
        FileDialogRequest request = dialog_request_for(mutable_node(*node));
        void* parent = m_native ? m_native->native_handle() : nullptr;
        PendingDialog pending;
        pending.node = node;
        pending.result = std::async(std::launch::async, [request, parent] { return show_file_dialog(request, parent); });
        m_dialogs_pending.emplace(node, std::move(pending));
    }

    void poll_dialogs() {
        for (auto it = m_dialogs_pending.begin(); it != m_dialogs_pending.end();) {
            if (it->second.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const Node* node = it->first;
            std::vector<std::string> paths = it->second.result.get();
            it = m_dialogs_pending.erase(it);
            if (!document().is_attached(*node)) continue;
            m_last_activity = std::chrono::steady_clock::now();
            if (paths.empty()) {
                Event cancel;
                cancel.type = "cancel";
                dispatch(node, cancel);
                continue;
            }
            std::string joined;
            for (const std::string& p : paths) joined += (joined.empty() ? "" : "\n") + p;
            set_input_string(document(), mutable_node(*node), joined);
            fire_value(*node, true);
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
    std::vector<ColorSwatch> m_swatches;
    const Node* m_modal = nullptr;
    Rect m_modal_panel;
    std::size_t m_modal_boxes = 0;
    std::size_t m_modal_hits = 0;
    ColorPicker m_picker;
    CodeHover m_code_hover;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    std::shared_ptr<fizmo::images::BitmapImage> m_color_wheel_image;
    std::shared_ptr<fizmo::graphics::Texture> m_color_wheel;
    std::vector<ElementBox> m_boxes;
    std::vector<const Node*> m_hover;
    std::vector<const Node*> m_active;
    const Node* m_pressed = nullptr;
    const Node* m_focused = nullptr;
    const Node* m_focus = nullptr;
    std::string m_edit;
    std::string m_focus_start;
    bool m_invalid = false;
    bool m_edit_all = false;
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
    std::unordered_map<std::string, float> m_measure;
    std::vector<Rect> m_clips;
    std::vector<Scroller> m_scrollers;
    std::unordered_map<const Node*, float> m_elem_scroll;
    std::unordered_map<const Node*, CodeEditor> m_code;
    const Node* m_code_focus = nullptr;
    const Node* m_code_drag = nullptr;
    std::unordered_map<const Node*, PendingDialog> m_dialogs_pending;
    float m_avail_h = -1.0f;
    std::unordered_map<std::string, std::shared_ptr<fizmo::graphics::Texture>> m_textures;

public:
    bool idle() {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [node, pos] : m_switch_pos) {
            const float target = input_bool(*node).value_or(false) ? 1.0f : 0.0f;
            if (pos != target) m_last_activity = now;
        }
        if (m_dragging || m_pressed || m_code_drag || !m_dialogs_pending.empty()) m_last_activity = now;
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
        host.file_dialog = [this](const FileDialogRequest& request) {
            WindowHost* wh = request.owner ? host_containing(*request.owner) : host_for(&m_main);
            void* parent = wh && wh->window && wh->window->is_open() ? wh->window->native_handle() : nullptr;
            return show_file_dialog(request, parent);
        };
        host.set_clipboard = [this](const std::string& text) {
            WindowHost* wh = host_for(&m_main);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            return wh->window->set_clipboard_text(text);
        };
        host.clipboard = [this]() {
            WindowHost* wh = host_for(&m_main);
            if (!wh || !wh->window || !wh->window->is_open()) return std::string();
            return wh->window->clipboard_text();
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
        host.file_dialog = nullptr;
        host.set_clipboard = nullptr;
        host.clipboard = nullptr;
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
        std::string app_id = element.attribute_or("app-id", "");
        if (app_id.empty() && !main) app_id = m_main.attribute_or("app-id", "");
        if (!app_id.empty()) host->window->set_app_id(app_id);
        std::vector<fizmo::images::BitmapImage> icons;
        for (const std::string& path : icon_paths(m_project, element)) {
            if (auto bitmap = load_bitmap(path)) icons.push_back(std::move(*bitmap));
        }
        if (!icons.empty()) host->window->set_icons(icons);
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
