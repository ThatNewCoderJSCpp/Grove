#include "grove/input/completion.hpp"
#include "grove/terminal.hpp"
#include "grove/canvas.hpp"
#include "grove/runtime/runtime.hpp"
#include "grove/pages.hpp"
#include "grove/markup/tags.hpp"
#include "grove/api/grove_api.h"
#include "grove/input/input.hpp"
#include "grove/input/code_buffer.hpp"
#include "grove/input/code_find.hpp"
#include "grove/input/keymap.hpp"
#include "grove/math_input.hpp"
#include "grove/native.hpp"
#include "grove/script/loader.hpp"
#include "grove/windowing.hpp"

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fizmo/includes.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <cctype>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace grove {
namespace runtime {

namespace {

using fizmo::colors::Color;

Color to_color(const bark::Rgba& c) { return Color(c.r, c.g, c.b, c.a); }

fizmo::windows::Window* g_clipboard_window = nullptr;

bool put_clipboard(fizmo::windows::Window* window, const std::string& text) {
    if (!window) return false;
    const bool ok = window->set_clipboard_text(text);
    if (ok) g_clipboard_window = window;
    return ok;
}

std::string read_clipboard(fizmo::windows::Window* window) {
    fizmo::windows::Window* from = g_clipboard_window && g_clipboard_window->is_open() ? g_clipboard_window : window;
    return from ? from->clipboard_text() : std::string();
}

void forget_clipboard_window(const fizmo::windows::Window* window) {
    if (window && g_clipboard_window == window) g_clipboard_window = nullptr;
}

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

[[maybe_unused]] void pop_code_point(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) return;
    }
}

std::size_t next_code_point(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) ++at;
    return at;
}

std::size_t previous_code_point(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    at = std::min(at, s.size()) - 1;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) --at;
    return at;
}

bool word_byte(unsigned char c) {
    return c >= 0x80 || std::isalnum(c) || c == '_';
}

std::size_t word_left(const std::string& s, std::size_t at) {
    at = std::min(at, s.size());
    while (at > 0 && !word_byte(static_cast<unsigned char>(s[at - 1]))) --at;
    while (at > 0 && word_byte(static_cast<unsigned char>(s[at - 1]))) --at;
    return at;
}

std::size_t word_right(const std::string& s, std::size_t at) {
    at = std::min(at, s.size());
    while (at < s.size() && !word_byte(static_cast<unsigned char>(s[at]))) ++at;
    while (at < s.size() && word_byte(static_cast<unsigned char>(s[at]))) ++at;
    return at;
}

std::size_t code_points_before(const std::string& s, std::size_t at) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < std::min(at, s.size()); ++i) n += (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 ? 1 : 0;
    return n;
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

const std::uint8_t k_box_arms[128] = { 5,10,80,160,5,10,80,160,5,10,80,160,68,72,132,136,65,66,129,130,20,24,36,40,17,18,33,34,84,88,100,148,164,104,152,168,81,82,97,145,161,98,146,162,69,70,73,74,133,134,137,138,21,22,25,26,37,38,41,42,85,86,89,90,101,149,165,102,105,150,153,106,154,166,169,170,5,10,80,160,15,240,76,196,204,67,193,195,28,52,60,19,49,51,92,244,252,83,241,243,79,197,207,31,53,63,95,245,255,68,65,17,20,0,0,0,1,16,4,64,2,32,8,128,9,144,6,96 };

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
constexpr float k_far = 1e7f;

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
    bool strikethrough = false;
};

bool is_strike_word(const bark::Value& v) {
    return v.is_ident("strikethrough") || v.is_ident("line-through") || v.is_ident("strike");
}

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
    look.strikethrough = false;
    for (const bark::Value& v : *values) {
        if (v.is_ident("bold")) look.bold = true;
        else if (v.is_ident("italic")) look.italic = true;
        else if (v.is_ident("underline")) look.underline = true;
        else if (is_strike_word(v)) look.strikethrough = true;
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
        else if (is_strike_word(v)) look.strikethrough = true;
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
    std::uint64_t seen_stamp = 0;
    std::vector<std::uint64_t> line_hashes;
    int states_language = -1;
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
    bool box_mode = false;
    std::size_t box_line = 0;
    std::size_t box_column = 0;
    CodeOverlay overlay;
    std::string overlay_spans;
    std::string overlay_problems;
    CodeOverlay links;
    std::string links_attr;
    std::string find_key;
    std::string find_query;
    std::string find_replace;
    CodeFindOptions find_options;
    std::size_t find_revision = static_cast<std::size_t>(-1);
    std::vector<CodeMatch> matches;
    std::size_t find_current = static_cast<std::size_t>(-1);
    bool find_bad = false;
    bool find_new_query = false;
    std::string find_request;
    std::string find_pending;
    std::size_t reported_current = static_cast<std::size_t>(-2);
    std::size_t reported_count = static_cast<std::size_t>(-2);
    bool reported_bad = false;
};

struct Scroller {
    const Node* node = nullptr;
    Rect rect;
    float max = 0;
    bool code = false;
    float max_x = 0;
    float bar_x = 0;
    float track_y = 0;
    float track_h = 0;
    float thumb_h = 0;
    float bar_y = 0;
    float track_x = 0;
    float track_w = 0;
    float thumb_w = 0;
};

struct BarDrag {
    const Node* node = nullptr;
    bool code = false;
    bool horizontal = false;
    float grab = 0;
    float start = 0;
    float length = 0;
    float thumb = 0;
    float max = 0;
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
    enum class Kind { Fill, Stroke, Circle, CircleStroke, Text, Math, ClipPush, ClipPop, Image, Line } kind = Kind::Fill;
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

struct FrameStatsLog {
    using Clock = std::chrono::steady_clock;
    bool enabled = std::getenv("GROVE_FRAME_STATS") != nullptr;
    Clock::time_point since = Clock::now();
    std::size_t frames = 0;
    std::size_t skipped = 0;
    double layout_ms = 0.0;
    double draw_ms = 0.0;
    double present_ms = 0.0;
    double worst_ms = 0.0;
    std::size_t ops = 0;
    std::size_t drawn = 0;
    std::size_t batches = 0;
    std::size_t replayed = 0;
    std::size_t culled = 0;
    std::size_t laid = 0;
    std::size_t dynamic = 0;

    static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

    void report() {
        if (!enabled) return;
        const Clock::time_point now = Clock::now();
        const double span = std::chrono::duration<double>(now - since).count();
        if (span < 2.0) return;
        const double n = frames == 0 ? 1.0 : static_cast<double>(frames);
        std::fprintf(stderr, "grove: %.1fs  %zu frames (%zu skipped)  layout %.2fms  draw %.2fms  present %.2fms  worst %.2fms  ops %zu  drawn %zu  batches %zu  cache: replayed %zu culled %zu laid %zu dynamic %zu\n",
                     span, frames, skipped, layout_ms / n, draw_ms / n, present_ms / n, worst_ms, static_cast<std::size_t>(ops / n), static_cast<std::size_t>(drawn / n), static_cast<std::size_t>(batches / n),
                     static_cast<std::size_t>(replayed / n), static_cast<std::size_t>(culled / n), static_cast<std::size_t>(laid / n), static_cast<std::size_t>(dynamic / n));
        *this = FrameStatsLog{};
        enabled = true;
        since = now;
    }
};

FrameStatsLog& frame_stats_log() {
    static FrameStatsLog log;
    return log;
}

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

struct SelectMenu {
    const Node* node = nullptr;
    Rect anchor;
    bool anchored = false;
    Rect panel;
    float scroll = 0;
    float max_scroll = 0;
    float row_h = 0;
    std::size_t active = 0;
    bool reveal = true;
    std::vector<std::pair<Rect, std::size_t>> rows;
};

struct CodeCompletion {
    const Node* node = nullptr;
    bool open = false;
    CodePos start;
    std::vector<CompletionItem> items;
    std::vector<CompletionMatch> shown;
    std::size_t active = 0;
    std::size_t top = 0;
    std::size_t visible = 0;
    Rect panel;
    float row_h = 0;
    std::vector<std::pair<Rect, std::size_t>> rows;
    const Node* suppressed_node = nullptr;
    CodePos suppressed_start{ static_cast<std::size_t>(-1), 0 };
    bool busy = false;
    std::string attr_seen;
    const Node* waiting = nullptr;
    CodePos waiting_start;
    bool waiting_manual = false;
};

struct TextRun {
    const Node* block = nullptr;
    const Node* owner = nullptr;
    std::size_t start = 0;
    std::string text;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    fizmo::text::TextStyle style;
    Rect clip;
    bool selectable = true;
};

struct TextPoint {
    const Node* block = nullptr;
    std::size_t offset = 0;
    bool valid() const noexcept { return block != nullptr; }
    bool operator==(const TextPoint& o) const noexcept { return block == o.block && offset == o.offset; }
};

struct TermView {
    const Node* node = nullptr;
    float x = 0;
    float y = 0;
    float cell_w = 1;
    float cell_h = 1;
    int cols = 1;
    int rows = 1;

    bool contains(float px, float py) const noexcept { return px >= x && py >= y && px < x + cell_w * static_cast<float>(cols) && py < y + cell_h * static_cast<float>(rows); }
};

struct ContextMenu {
    const Node* owner = nullptr;
    std::vector<MenuItem> items;
    float x = 0;
    float y = 0;
    Rect panel;
    float row_h = 0;
    std::size_t active = static_cast<std::size_t>(-1);
    std::vector<std::pair<Rect, std::size_t>> rows;
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
    float scroll = 0;
    float max_scroll = 0;
    std::string shown_text;
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
    if (name == "col-resize" || name == "ew-resize" || name == "e-resize" || name == "w-resize") return SystemCursor::ResizeHorizontal;
    if (name == "row-resize" || name == "ns-resize" || name == "n-resize" || name == "s-resize") return SystemCursor::ResizeVertical;
    if (name == "nwse-resize" || name == "nw-resize" || name == "se-resize") return SystemCursor::ResizeDiagonalNWSE;
    if (name == "nesw-resize" || name == "ne-resize" || name == "sw-resize") return SystemCursor::ResizeDiagonalNESW;
    if (name == "grab" || name == "grabbing" || name == "all-scroll") return SystemCursor::Move;
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
    double last_layout_ms() const noexcept { return m_last_layout_ms; }
    double last_draw_ms() const noexcept { return m_last_draw_ms; }
    std::size_t last_ops() const noexcept { return m_last_ops; }

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

    void apply_zoom() {
        double zoom = 1.0;
        if (const bark::ValueList* v = style_of(m_window).get("zoom"); v && !v->empty() && v->front().is_number()) {
            const bark::Value& z = v->front();
            zoom = z.unit == "%" ? z.number / 100.0 : z.unit.empty() ? z.number : 1.0;
        }
        zoom = std::clamp(zoom, 0.25, 5.0);
        bark::Viewport vp = m_styles.viewport();
        if (vp.zoom == zoom) return;
        vp.zoom = zoom;
        m_styles.set_viewport(vp);
        m_measure.clear();
    }

    void paint(fizmo::graphics::Renderer& r) {
        m_renderer = &r;
        const auto now = std::chrono::steady_clock::now();
        FrameStatsLog& stats = frame_stats_log();
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
            m_styles.set_viewport(bark::Viewport{ w, h, m_styles.viewport().zoom });
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
            apply_zoom();
            const std::string title = title_now();
            if (title != m_title) {
                m_title = title;
                if (m_on_title) m_on_title(title);
            }
            if (m_focus && !document().is_attached(*m_focus)) blur(false);
        }

        poll_dialogs();
        ++m_frame_index;
        prune_layout_cache();
        if (m_picker.node && !document().is_attached(*m_picker.node)) m_picker = ColorPicker{};
        if (m_menu.node && !document().is_attached(*m_menu.node)) m_menu = SelectMenu{};
        m_menu.anchored = false;
        m_swatches.clear();
        m_ops.clear();
        m_hits.clear();
        m_term_views.clear();
        m_runs.clear();
        m_run_refs.clear();
        m_partial_runs = false;
        if (m_pending_select && !document().is_attached(*m_pending_select)) {
            m_pending_select = nullptr;
            m_force_full = false;
        }
        if (m_sel_anchor.valid() && (!document().is_attached(*m_sel_anchor.block) || !document().is_attached(*m_sel_focus.block))) clear_text_selection();
        m_boxes.clear();
        m_opacity.clear();
        m_scrollers.clear();
        m_clips.clear();
        m_code_laid.clear();
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
        layout_completion(r, w, h);
        layout_picker(r, w, h);
        layout_menu(r, w, h);
        layout_context(r, w, h);
        layout_floating(r, w, h);
        publish_canvases();
        if (m_pending_select && m_force_full) {
            const Node* pending = m_pending_select;
            m_pending_select = nullptr;
            m_force_full = false;
            select_contents(*pending);
        }
        const auto laid = std::chrono::steady_clock::now();
        std::size_t drawn = 0;
        std::size_t batches = 0;
        int last_kind = -1;
        const void* last_texture = nullptr;
        build_draw_list();
        std::vector<Rect> clips;
        auto apply = [&r](const Rect& c) {
            const int cx = static_cast<int>(std::floor(c.x));
            const int cy = static_cast<int>(std::floor(c.y));
            r.set_clip_rect(cx, cy, static_cast<unsigned>(std::max(0.0f, std::ceil(c.x + c.w) - static_cast<float>(cx))), static_cast<unsigned>(std::max(0.0f, std::ceil(c.y + c.h) - static_cast<float>(cy))));
        };
        for (const DrawOp* opp : m_draw_list) {
            const DrawOp& op = *opp;
            if (op.kind == DrawOp::Kind::ClipPush) {
                Rect c{ op.x, op.y, op.w, op.h };
                if (!clips.empty()) c = c.intersect(clips.back());
                clips.push_back(c);
                apply(c);
                last_kind = -1;
            } else if (op.kind == DrawOp::Kind::ClipPop) {
                if (!clips.empty()) clips.pop_back();
                if (clips.empty()) r.reset_clip_rect();
                else apply(clips.back());
                last_kind = -1;
            } else {
                if (stats.enabled) {
                    const int kind = op.kind == DrawOp::Kind::Text || op.kind == DrawOp::Kind::Math ? 1 : op.kind == DrawOp::Kind::Image ? 2 : 0;
                    const void* texture = op.kind == DrawOp::Kind::Image ? static_cast<const void*>(op.texture) : nullptr;
                    if (kind != last_kind || texture != last_texture) ++batches;
                    last_kind = kind;
                    last_texture = texture;
                    ++drawn;
                }
                draw(r, op);
            }
        }
        r.reset_clip_rect();
        run_find_requests();
        report_cursors();
        {
            const auto finished = std::chrono::steady_clock::now();
            m_last_layout_ms = FrameStatsLog::ms(now, laid);
            m_last_draw_ms = FrameStatsLog::ms(laid, finished);
            m_last_ops = m_ops.size();
        }
        if (stats.enabled) {
            const auto done = std::chrono::steady_clock::now();
            stats.layout_ms += FrameStatsLog::ms(now, laid);
            stats.draw_ms += FrameStatsLog::ms(laid, done);
            stats.ops += m_ops.size();
            stats.drawn += drawn;
            stats.batches += batches;
        }
    }

    void on_event(const fizmo::windows::WindowEvent& e) {
        using fizmo::windows::WindowEventType;
        m_last_activity = std::chrono::steady_clock::now();
        if (m_blocked && m_blocked()) {
            if (e.type == WindowEventType::MouseClick && m_on_blocked) m_on_blocked();
            if (e.type == WindowEventType::MouseClick || e.type == WindowEventType::MouseRelease || e.type == WindowEventType::MouseDoubleClick || e.type == WindowEventType::KeyPress || e.type == WindowEventType::KeyRelease || e.type == WindowEventType::TextInput || e.type == WindowEventType::MouseScroll || e.type == WindowEventType::FilesDropped) return;
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
            case WindowEventType::FilesDropped: {
                if (e.paths.empty() || m_context.owner) break;
                Event ev = pointer_event("drop", px, py, 0);
                ev.paths = e.paths;
                for (const std::string& p : e.paths) {
                    if (!ev.text.empty()) ev.text += '\n';
                    ev.text += p;
                }
                m_mouse_x = px;
                m_mouse_y = py;
                dispatch(hit_element(px, py), ev);
                poke();
                break;
            }
            case WindowEventType::MouseMove:
                m_mouse_x = px;
                m_mouse_y = py;
                if (m_bar_drag.node) {
                    bar_drag_to(px, py);
                    break;
                }
                if (m_dragging) drag_to(px);
                if (m_code_drag) code_drag_to(px, py);
                if (m_picker.drag != PickerPart::None) picker_drag(px, py);
                term_mouse_move(px, py);
                if (m_edit_dragging && m_focus && document().is_attached(*m_focus)) {
                    m_edit_click_x = px;
                    m_edit_click_y = py;
                    m_edit_click_extend = true;
                    mutable_node(*m_focus).mark_changed(false);
                    poke();
                }
                text_drag_to(px, py);
                hover_at(px, py);
                code_hover_move(px, py);
                if (document().events().has_listeners("mouse_move")) {
                    Event ev = pointer_event("mouse_move", px, py, 0);
                    dispatch(hit_element(px, py), ev);
                }
                break;
            case WindowEventType::MouseClick:
                if (m_context.owner) {
                    if (m_context.panel.contains(px, py)) {
                        if (e.button == 1) {
                            for (const auto& [row, index] : m_context.rows) {
                                if (row.contains(px, py)) {
                                    choose_context(index);
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    close_context_menu(true);
                    if (e.button == 1) break;
                }
                if (term_mouse_down(px, py, static_cast<int>(e.button))) break;
                if (e.button == 1) mouse_down(px, py);
                else {
                    Event ev = pointer_event("mouse_down", px, py, static_cast<int>(e.button));
                    const Node* under = hit_element(px, py);
                    if (dispatch(under, ev) && e.button == 2) field_menu(under, static_cast<float>(px), static_cast<float>(py));
                }
                break;
            case WindowEventType::MouseRelease:
                mouse_up(px, py, static_cast<int>(e.button));
                break;
            case WindowEventType::MouseDoubleClick: {
                if (m_context.owner) break;
                if (term_double_click(px, py)) break;
                Event ev = pointer_event("double_click", px, py, static_cast<int>(e.button));
                const Node* under = hit_element(px, py);
                if (dispatch(under, ev) && e.button == 1) {
                    if (code_node_at(px, py)) code_double_click(px, py);
                    else if (selectable_target(under)) {
                        select_word_at(px, py);
                        m_sel_pending = false;
                    }
                }
                break;
            }
            case WindowEventType::MouseScroll: {
                if (m_context.owner) close_context_menu(true);
                const double dy = e.scroll_y != 0.0f ? static_cast<double>(e.scroll_y) : static_cast<double>(e.scroll_delta);
                if (completion_wheel(dy)) break;
                if (!m_modal && e.scroll_x == 0.0f && term_wheel(m_mouse_x, m_mouse_y, dy)) break;
                Event ev = pointer_event("wheel", m_mouse_x, m_mouse_y, 0);
                ev.delta_x = e.scroll_x;
                ev.delta_y = dy;
                if (m_modal && !m_modal_panel.contains(m_mouse_x, m_mouse_y)) break;
                if (m_code_hover.shown && m_code_hover.panel.contains(m_mouse_x, m_mouse_y)) {
                    if (m_code_hover.max_scroll > 0.0f) {
                        m_code_hover.scroll = std::clamp(m_code_hover.scroll - static_cast<float>(dy) * 48.0f, 0.0f, m_code_hover.max_scroll);
                        poke();
                        break;
                    }
                } else {
                    hide_code_hover();
                }
                if (m_menu.node && m_menu.panel.contains(m_mouse_x, m_mouse_y)) {
                    m_menu.scroll = std::clamp(m_menu.scroll - static_cast<float>(dy) * m_menu.row_h * 3.0f, 0.0f, m_menu.max_scroll);
                    break;
                }
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

    std::string public_selected_text() { return selection_text(); }
    bool public_select(const Node& node) { return select_contents(node); }
    void public_clear_selection() { clear_text_selection(); }

    bool open_context_menu(Node& owner, const std::vector<MenuItem>& items, double x, double y) {
        if (items.empty()) return false;
        close_menu();
        hide_code_hover();
        m_context = ContextMenu{};
        m_context.owner = &owner;
        m_context.items = items;
        m_context.x = static_cast<float>(x);
        m_context.y = static_cast<float>(y);
        poke();
        return true;
    }

    bool close_context_menu(bool tell) {
        if (!m_context.owner) return false;
        const Node* owner = m_context.owner;
        m_context = ContextMenu{};
        if (tell && document().is_attached(*owner)) {
            Event e;
            e.type = "menu_close";
            e.bubbles = false;
            dispatch(owner, e);
        }
        poke();
        return true;
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

    bool computed_number(const Node& node, const std::string& property, std::size_t index, double& out) {
        const bark::ComputedStyle& style = style_of(node);
        const bark::ValueList* v = style.get(property);
        if (!v || index >= v->size()) return false;
        const bark::Value& value = (*v)[index];
        if (value.is_number() && value.unit.empty()) {
            out = value.number;
            return true;
        }
        if (const auto px = resolve_length(style, property, m_view_w, index)) {
            out = *px;
            return true;
        }
        if (value.is_ident("true") || value.is_ident("yes") || value.is_ident("on")) {
            out = 1.0;
            return true;
        }
        if (value.is_ident("false") || value.is_ident("no") || value.is_ident("off")) {
            out = 0.0;
            return true;
        }
        return false;
    }

    std::string computed_color(const Node& node, const std::string& property) {
        const auto c = style_of(node).color(property);
        if (!c) return {};
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
        return buf;
    }

private:
    const bark::ComputedStyle& style_of(const Node& node) { return m_styles.style_of(node, m_project.diagnostics); }
    Node& mutable_node(const Node& node) { return const_cast<Node&>(node); }

    bool dispatch(const Node* target, Event& e) {
        e.modifiers = m_mods;
        if (target && target != &m_window && !document().is_attached(*target)) target = &m_window;
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
        const bool control = !e.key.empty() && (static_cast<unsigned char>(e.key[0]) < 0x20 || static_cast<unsigned char>(e.key[0]) == 0x7F);
        if (e.key.empty() || control) {
            const fizmo::input::Key k = w.logical_key != fizmo::input::Key::Unknown ? w.logical_key : w.physical_key;
            if (const char* name = fizmo::input::key_name(k)) e.key = name;
        }
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

    static int draw_class(const DrawOp& op) {
        switch (op.kind) {
            case DrawOp::Kind::Text: return 1;
            case DrawOp::Kind::Image: return 2;
            case DrawOp::Kind::Math: return 3;
            default: return 0;
        }
    }

    static Rect op_rect(const DrawOp& op) {
        if (op.kind == DrawOp::Kind::Circle || op.kind == DrawOp::Kind::CircleStroke) return Rect{ op.x - op.radius - 1.0f, op.y - op.radius - 1.0f, op.radius * 2.0f + 2.0f, op.radius * 2.0f + 2.0f };
        if (op.kind == DrawOp::Kind::Math) return Rect{ op.x - 4.0f, op.y - 4.0f, 1e6f, 1e6f };
        if (op.kind == DrawOp::Kind::Line) {
            const float pad = static_cast<float>(op.stroke) + 1.0f;
            const float x0 = std::min(op.x, op.x + op.w);
            const float y0 = std::min(op.y, op.y + op.h);
            return Rect{ x0 - pad, y0 - pad, std::fabs(op.w) + pad * 2.0f, std::fabs(op.h) + pad * 2.0f };
        }
        const float pad = static_cast<float>(op.stroke) + 1.0f;
        return Rect{ op.x - pad, op.y - pad, std::max(op.w, 1.0f) + pad * 2.0f, std::max(op.h, 1.0f) + pad * 2.0f };
    }

    static bool overlaps(const Rect& a, const Rect& b) {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    }

    struct DrawShape {
        Rect rect;
        float band = 0.0f;
    };

    static DrawShape op_shape(const DrawOp& op) {
        DrawShape shape{ op_rect(op), 0.0f };
        if (op.kind == DrawOp::Kind::Stroke && op.w > 8.0f && op.h > 8.0f) shape.band = static_cast<float>(op.stroke) + std::max(2.0f, op.radius * 0.35f) + 1.0f;
        return shape;
    }

    static bool shape_hits(const DrawShape& s, const Rect& q) {
        if (!overlaps(s.rect, q)) return false;
        if (s.band <= 0.0f || s.band * 2.0f >= std::min(s.rect.w, s.rect.h)) return true;
        const Rect& r = s.rect;
        return overlaps(Rect{ r.x, r.y, r.w, s.band }, q) || overlaps(Rect{ r.x, r.y + r.h - s.band, r.w, s.band }, q) || overlaps(Rect{ r.x, r.y, s.band, r.h }, q) ||
               overlaps(Rect{ r.x + r.w - s.band, r.y, s.band, r.h }, q);
    }

    static bool shapes_overlap(const DrawShape& a, const DrawShape& b) {
        if (a.band > 0.0f && b.band > 0.0f) {
            if (!overlaps(a.rect, b.rect)) return false;
            const Rect& r = b.rect;
            return shape_hits(a, Rect{ r.x, r.y, r.w, b.band }) || shape_hits(a, Rect{ r.x, r.y + r.h - b.band, r.w, b.band }) || shape_hits(a, Rect{ r.x, r.y, b.band, r.h }) ||
                   shape_hits(a, Rect{ r.x + r.w - b.band, r.y, b.band, r.h });
        }
        if (a.band > 0.0f) return shape_hits(a, b.rect);
        if (b.band > 0.0f) return shape_hits(b, a.rect);
        return overlaps(a.rect, b.rect);
    }

    struct DrawLayer {
        int cls = 0;
        const void* texture = nullptr;
        Rect box;
        std::vector<const DrawOp*> items;
        std::vector<DrawShape> rects;
    };

    void batch_segment(std::vector<const DrawOp*>& segment) {
        if (segment.size() < 3) {
            m_draw_list.insert(m_draw_list.end(), segment.begin(), segment.end());
            segment.clear();
            return;
        }
        std::vector<DrawLayer>& layers = m_draw_layers;
        layers.clear();
        for (const DrawOp* op : segment) {
            const int cls = draw_class(*op);
            const void* texture = op->kind == DrawOp::Kind::Image ? static_cast<const void*>(op->texture) : nullptr;
            const DrawShape shape = op_shape(*op);
            const Rect rect = shape.rect;
            std::size_t target = layers.size();
            const std::size_t floor = layers.size() > 12 ? layers.size() - 12 : 0;
            for (std::size_t k = layers.size(); k-- > floor;) {
                DrawLayer& layer = layers[k];
                if (layer.cls == cls && layer.texture == texture && cls != 3) {
                    target = k;
                    break;
                }
                if (!overlaps(layer.box, rect)) continue;
                bool hit = layer.rects.size() > 48;
                for (std::size_t q = 0; !hit && q < layer.rects.size(); ++q) hit = shapes_overlap(layer.rects[q], shape);
                if (hit) break;
            }
            if (target == layers.size()) {
                DrawLayer fresh;
                fresh.cls = cls;
                fresh.texture = texture;
                fresh.box = rect;
                layers.push_back(std::move(fresh));
            }
            DrawLayer& layer = layers[target];
            if (!layer.items.empty()) {
                const float l = std::min(layer.box.x, rect.x);
                const float t = std::min(layer.box.y, rect.y);
                const float rr = std::max(layer.box.x + layer.box.w, rect.x + rect.w);
                const float b = std::max(layer.box.y + layer.box.h, rect.y + rect.h);
                layer.box = Rect{ l, t, rr - l, b - t };
            }
            layer.items.push_back(op);
            layer.rects.push_back(shape);
        }
        for (const DrawLayer& layer : layers) m_draw_list.insert(m_draw_list.end(), layer.items.begin(), layer.items.end());
        segment.clear();
    }

    void find_redundant_clips() {
        m_redundant_clip.assign(m_ops.size(), 0);
        struct Open {
            std::size_t at;
            Rect clip;
            bool fits;
        };
        std::vector<Open> open;
        for (std::size_t i = 0; i < m_ops.size(); ++i) {
            const DrawOp& op = m_ops[i];
            if (op.kind == DrawOp::Kind::ClipPush) {
                open.push_back(Open{ i, Rect{ op.x, op.y, op.w, op.h }, true });
                continue;
            }
            if (op.kind == DrawOp::Kind::ClipPop) {
                if (open.empty()) continue;
                const Open done = open.back();
                open.pop_back();
                if (done.fits) {
                    m_redundant_clip[done.at] = 1;
                    m_redundant_clip[i] = 1;
                } else if (!open.empty()) {
                    open.back().fits = false;
                }
                continue;
            }
            if (open.empty()) continue;
            Open& top = open.back();
            if (!top.fits) continue;
            const Rect r = op.kind == DrawOp::Kind::Stroke || op.kind == DrawOp::Kind::Circle || op.kind == DrawOp::Kind::CircleStroke ? op_rect(op) : Rect{ op.x, op.y, op.w, op.h };
            const float slack = 1.5f;
            if (op.kind == DrawOp::Kind::Math || r.x + slack < top.clip.x || r.y + slack < top.clip.y || r.x + r.w - slack > top.clip.x + top.clip.w || r.y + r.h - slack > top.clip.y + top.clip.h) top.fits = false;
        }
        for (const Open& o : open) m_redundant_clip[o.at] = 0;
    }

    void build_draw_list() {
        m_draw_list.clear();
        m_draw_list.reserve(m_ops.size());
        if (m_batching) find_redundant_clips();
        std::vector<Rect> clips;
        std::vector<const DrawOp*> segment;
        for (std::size_t index = 0; index < m_ops.size(); ++index) {
            const DrawOp& op = m_ops[index];
            if (op.kind == DrawOp::Kind::ClipPush || op.kind == DrawOp::Kind::ClipPop) {
                if (m_batching && m_redundant_clip[index]) continue;
                batch_segment(segment);
                if (op.kind == DrawOp::Kind::ClipPush) {
                    Rect c{ op.x, op.y, op.w, op.h };
                    if (!clips.empty()) c = c.intersect(clips.back());
                    clips.push_back(c);
                } else if (!clips.empty()) {
                    clips.pop_back();
                }
                m_draw_list.push_back(&op);
                continue;
            }
            if (!clips.empty() && !op_visible(op, clips.back())) continue;
            if ((op.kind == DrawOp::Kind::Fill || op.kind == DrawOp::Kind::Stroke) && op.color.alpha() == 0) continue;
            if (!m_batching) m_draw_list.push_back(&op);
            else segment.push_back(&op);
        }
        batch_segment(segment);
    }

    static bool op_visible(const DrawOp& op, const Rect& clip) {
        float x = op.x;
        float y = op.y;
        float w = std::max(op.w, 1.0f);
        float h = std::max(op.h, 1.0f);
        switch (op.kind) {
            case DrawOp::Kind::Math: return true;
            case DrawOp::Kind::Line: return true;
            case DrawOp::Kind::Circle:
            case DrawOp::Kind::CircleStroke:
                x = op.x - op.radius;
                y = op.y - op.radius;
                w = h = op.radius * 2.0f;
                break;
            default: break;
        }
        const float pad = static_cast<float>(op.stroke) + 2.0f;
        return x - pad < clip.x + clip.w && x + w + pad > clip.x && y - pad < clip.y + clip.h && y + h + pad > clip.y;
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
            case DrawOp::Kind::Line: r.draw_line(x, y, static_cast<int>(std::lround(op.x + op.w)), static_cast<int>(std::lround(op.y + op.h)), fizmo::graphics::Paint::stroke(op.color, op.stroke)); break;
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
        if (style.text_view("font-weight") == "bold") ts.set_bold(true);
        if (style.text_view("font-style") == "italic") ts.set_italic(true);
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
        const double size = style.size();
        const int font = static_cast<int>(style.font());
        std::string key(sizeof(size) + 1 + sizeof(font), '\0');
        std::memcpy(&key[0], &size, sizeof(size));
        key[sizeof(size)] = static_cast<char>((style.is_bold() ? 1 : 0) | (style.is_italic() ? 2 : 0));
        std::memcpy(&key[sizeof(size) + 1], &font, sizeof(font));
        return key;
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
        std::size_t codes = 0;
        std::size_t terms = 0;
        std::size_t runs = 0;
        std::size_t run_refs = 0;
    };

    Mark mark() const { return Mark{ m_ops.size(), m_hits.size(), m_boxes.size(), m_scrollers.size(), m_clips.size(), m_swatches.size(), m_code_laid.size(), m_term_views.size(), m_runs.size(), m_run_refs.size() }; }

    struct RunRef {
        std::size_t at = 0;
        const std::vector<TextRun>* runs = nullptr;
        float dx = 0.0f;
        float dy = 0.0f;
        float clip_dx = 0.0f;
        Rect clip;
    };

    std::vector<TextRun>& runs() {
        if (m_run_refs.empty()) return m_runs;
        std::vector<TextRun> merged;
        std::size_t extra = 0;
        for (const RunRef& ref : m_run_refs) extra += ref.runs->size();
        merged.reserve(m_runs.size() + extra);
        std::size_t next = 0;
        for (const RunRef& ref : m_run_refs) {
            const std::size_t at = std::min(ref.at, m_runs.size());
            for (; next < at; ++next) merged.push_back(std::move(m_runs[next]));
            for (const TextRun& t : *ref.runs) {
                merged.push_back(t);
                merged.back().x += ref.dx;
                merged.back().y += ref.dy;
                merged.back().clip = ref.clip;
                merged.back().clip.x += ref.clip_dx;
            }
        }
        for (; next < m_runs.size(); ++next) merged.push_back(std::move(m_runs[next]));
        m_runs = std::move(merged);
        m_run_refs.clear();
        return m_runs;
    }

    void shift_x(const Mark& from, float dx) {
        for (std::size_t i = from.ops; i < m_ops.size(); ++i) m_ops[i].x += dx;
        for (std::size_t i = from.hits; i < m_hits.size(); ++i) {
            m_hits[i].x += dx;
            m_hits[i].track_x += dx;
        }
        for (std::size_t i = from.boxes; i < m_boxes.size(); ++i) m_boxes[i].x += dx;
        for (std::size_t i = from.scrollers; i < m_scrollers.size(); ++i) {
            m_scrollers[i].rect.x += dx;
            m_scrollers[i].bar_x += dx;
            m_scrollers[i].track_x += dx;
        }
        for (std::size_t i = from.swatches; i < m_swatches.size(); ++i) {
            m_swatches[i].swatch.x += dx;
            m_swatches[i].box.x += dx;
        }
        for (std::size_t i = from.codes; i < m_code_laid.size(); ++i) m_code[m_code_laid[i]].text_x += dx;
        for (std::size_t i = from.terms; i < m_term_views.size(); ++i) m_term_views[i].x += dx;
        for (std::size_t i = from.run_refs; i < m_run_refs.size(); ++i) {
            m_run_refs[i].dx += dx;
            m_run_refs[i].clip_dx += dx;
        }
        for (std::size_t i = from.runs; i < m_runs.size(); ++i) {
            m_runs[i].x += dx;
            m_runs[i].clip.x += dx;
        }
    }

    void rewind(const Mark& m) {
        m_ops.resize(m.ops);
        m_hits.resize(m.hits);
        m_boxes.resize(m.boxes);
        m_scrollers.resize(m.scrollers);
        m_clips.resize(m.clips);
        m_swatches.resize(m.swatches);
        m_code_laid.resize(m.codes);
        m_term_views.resize(m.terms);
        m_runs.resize(m.runs);
        m_run_refs.resize(m.run_refs);
    }

    bool is_row(const Node& element, const bark::ComputedStyle& style) {
        const std::string_view display = style.text_view("display");
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
        const std::string_view display = style.text_view("display");
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
        const bark::ComputedStyle& style = style_of(node);
        return style.text_view("display") == "none" || floating(style);
    }

    static bool floating(const bark::ComputedStyle& style) {
        const std::string_view position = style.text_view("position");
        return position == "fixed" || position == "cursor";
    }

    struct ChildRecord {
        const Node* node = nullptr;
        std::uint64_t info_stamp = 0;
        std::uint64_t laid_stamp = 0;
        bool skip = false;
        bool inline_item = false;
        float flex = 0.0f;
        unsigned autos = 0;
        bool measured = false;
        float advance = 0.0f;
        Rect bounds;
    };

    struct ChildCache {
        std::uint64_t style = 0;
        std::uint64_t generation = 0;
        std::uint64_t used = 0;
        float width = 0.0f;
        float avail = 0.0f;
        float total_flex = 0.0f;
        unsigned autos = 0;
        unsigned inlines = 0;
        std::vector<ChildRecord> records;
    };

    void child_info(const Node& parent, ChildRecord& rec, bool preserve) {
        const Node& child = *rec.node;
        rec.info_stamp = child.tree_stamp();
        rec.flex = 0.0f;
        rec.autos = 0;
        rec.inline_item = false;
        rec.skip = skipped(child);
        if (rec.skip) return;
        if (is_inline_flow(child)) {
            if (child.is_text() && !preserve && collapse_whitespace(child.text()).empty()) {
                rec.skip = true;
                return;
            }
            rec.inline_item = true;
            return;
        }
        (void)parent;
        const bark::ComputedStyle& bs = style_of(child);
        rec.flex = flex_of(bs);
        const AutoSides a = auto_sides(bs, "margin");
        rec.autos = (a.top ? 1u : 0u) + (a.bottom ? 1u : 0u);
    }

    bool layout_children_cached(fizmo::graphics::Renderer& r, const Node& parent, float x, float& y, float width) {
        if (!m_layout_caching || m_capturing > 0 || m_force_full || m_sel_anchor.valid()) return false;
        const auto& kids = parent.children();
        if (kids.size() < 24) return false;
        const std::uint64_t key = m_styles.style_key(parent, m_project.diagnostics);
        const std::uint64_t generation = layout_generation();
        const float avail = m_avail_h;
        ChildCache& cache = m_child_cache[&parent];
        cache.used = m_frame_index;
        bool valid = cache.style == key && cache.generation == generation && cache.width == width && cache.avail == avail && cache.records.size() == kids.size();
        for (std::size_t i = 0; valid && i < kids.size(); ++i) valid = cache.records[i].node == kids[i].get();
        const bool preserve = preserves_space(parent);
        if (!valid) {
            cache = ChildCache{};
            cache.style = key;
            cache.generation = generation;
            cache.used = m_frame_index;
            cache.width = width;
            cache.avail = avail;
            cache.records.resize(kids.size());
            for (std::size_t i = 0; i < kids.size(); ++i) {
                ChildRecord& rec = cache.records[i];
                rec.node = kids[i].get();
                child_info(parent, rec, preserve);
                cache.total_flex += rec.flex;
                cache.autos += rec.autos;
                cache.inlines += rec.inline_item ? 1u : 0u;
            }
        } else {
            for (ChildRecord& rec : cache.records) {
                if (rec.info_stamp == rec.node->tree_stamp()) continue;
                cache.total_flex -= rec.flex;
                cache.autos -= rec.autos;
                cache.inlines -= rec.inline_item ? 1u : 0u;
                child_info(parent, rec, preserve);
                cache.total_flex += rec.flex;
                cache.autos += rec.autos;
                cache.inlines += rec.inline_item ? 1u : 0u;
            }
        }
        if (cache.inlines > 0) return false;
        const bark::ComputedStyle& parent_style = style_of(parent);
        const Alignment al = alignment_of(parent_style, false);
        if (avail >= 0.0f && (cache.total_flex > 0.0f || cache.autos > 0 || al.main != "start")) return false;
        for (ChildRecord& rec : cache.records) {
            if (rec.skip) continue;
            if (rec.measured && offscreen(rec.bounds, x, y)) {
                y += rec.advance;
                m_partial_runs = true;
                continue;
            }
            const Mark before = mark();
            const float y0 = y;
            m_avail_h = avail;
            place_block(r, *rec.node, x, y, width, al.cross, -1.0f);
            rec.advance = y - y0;
            rec.bounds = slice_bounds(before, x, y0, rec.advance);
            rec.laid_stamp = rec.node->tree_stamp();
            rec.measured = true;
        }
        m_avail_h = avail;
        return true;
    }

    void layout_children(fizmo::graphics::Renderer& r, const Node& parent, float x, float& y, float width) {
        if (layout_children_cached(r, parent, x, y, width)) return;
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
            if (n >= 0.0f) bw = m_wide > 0 && b.tag() == "img" ? n - m.left - m.right : std::min(avail, n - m.left - m.right);
            return bw + m.left + m.right;
        }
        if (const auto w = resolve_length(s, "width", width)) {
            bw = m_wide > 0 ? *w : std::min(avail, *w);
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

    static std::size_t tab_size_of(const bark::ComputedStyle& style) {
        const bark::ValueList* v = style.get("tab-size");
        if (v && !v->empty() && v->front().is_number() && v->front().number >= 1.0) return static_cast<std::size_t>(std::min(16.0, v->front().number));
        return 4;
    }

    static std::string expand_tab_lines(const std::string& text, std::size_t tab) {
        if (text.find('\t') == std::string::npos) return text;
        std::string out;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::size_t end = nl == std::string::npos ? text.size() : nl;
            out += code_expand_tabs(std::string_view(text).substr(start, end - start), tab);
            if (nl == std::string::npos) break;
            out += '\n';
            start = nl + 1;
        }
        return out;
    }

    bool preserves_space(const Node& owner) {
        const std::string ws = style_of(owner).text("white-space").value_or("normal");
        return ws == "pre" || ws == "pre-wrap";
    }

    float natural_width(fizmo::graphics::Renderer& r, const Node& owner, const Node& node, float row_width) {
        if (node.is_text()) return text_width(r, collapse_whitespace(node.text()), text_style(owner)) + 1.0f;
        if (!node.is_element()) return 0.0f;
        const bark::ComputedStyle& style = style_of(node);
        if (style.text_view("display") == "none" || (floating(style) && &node != m_floating_node)) return 0.0f;
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
            if (*kind == InputKind::Select) {
                if (const auto w = resolve_length(style, "width", row_width)) return *w + margins;
                float n = select_content_width(r, node);
                if (const auto mn = resolve_length(style, "min-width", row_width)) n = std::max(n, *mn);
                if (const auto mx = resolve_length(style, "max-width", row_width)) n = std::min(n, *mx);
                return n + margins;
            }
            if (is_text_input(*kind)) {
                if (style.get("flex")) return -1.0f;
                if (const auto w = resolve_length(style, "width", row_width)) return *w + margins;
                return 220.0f + margins;
            }
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
        const std::string_view display = style.text_view("display");
        return display == "inline-block" || (display.empty() && (node.tag() == "button" || node.tag() == "input_file"));
    }

    float wrap_width(fizmo::graphics::Renderer& r, const Node& row, const Node& item, float width) {
        float w = natural_width(r, row, item, width);
        if (!item.is_element()) return std::max(0.0f, w);
        const bark::ComputedStyle& cs = style_of(item);
        const Box m = box_of(cs, "margin");
        if (const auto v = resolve_length(cs, "min-width", width)) w = std::max(w, *v + m.left + m.right);
        if (w < 0.0f) w = 80.0f + m.left + m.right;
        return w;
    }

    void layout_row(fizmo::graphics::Renderer& r, const Node& element, const bark::ComputedStyle& style, float x, float& y, float width) {
        std::vector<const Node*> items;
        for (const auto& c : element.children()) {
            if (c->is_text() && collapse_whitespace(c->text()).empty()) continue;
            if (c->is_element() && (c->tag() == "window" || style_of(*c).text_view("display") == "none" || floating(style_of(*c)))) continue;
            if (c->is_element() || c->is_text()) items.push_back(c.get());
        }
        if (items.empty()) return;
        if (items.size() > 1 && style.text_view("flex-wrap") == "wrap") {
            const float gap = static_cast<float>(style.length("gap").value_or(8.0));
            const float row_gap = static_cast<float>(style.length("row-gap").value_or(gap));
            std::vector<std::vector<const Node*>> lines(1);
            float used = 0.0f;
            for (const Node* n : items) {
                const float w = wrap_width(r, element, *n, width);
                const float need = lines.back().empty() ? w : used + gap + w;
                if (!lines.back().empty() && need > width + 0.5f) {
                    lines.emplace_back();
                    used = w;
                } else {
                    used = need;
                }
                lines.back().push_back(n);
            }
            if (lines.size() > 1) {
                const float saved_h = m_avail_h;
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (i > 0) y += row_gap;
                    m_avail_h = -1.0f;
                    layout_row_items(r, element, style, lines[i], x, y, width);
                }
                m_avail_h = saved_h;
                return;
            }
        }
        layout_row_items(r, element, style, items, x, y, width);
    }

    void layout_row_items(fizmo::graphics::Renderer& r, const Node& element, const bark::ComputedStyle& style, const std::vector<const Node*>& items, float x, float& y, float width) {
        const float gap = static_cast<float>(style.length("gap").value_or(8.0));
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
        if (pre) {
            const std::size_t tab = tab_size_of(owner_style);
            std::size_t column = 0;
            for (Fragment& f : frags) {
                if (f.newline) {
                    column = 0;
                    continue;
                }
                if (f.text.find('\t') == std::string::npos) {
                    column += code_display_column(f.text, f.text.size(), tab);
                    continue;
                }
                std::string expanded;
                for (char c : f.text) {
                    if (c == '\t') {
                        const std::size_t n = tab - column % tab;
                        expanded.append(n, ' ');
                        column += n;
                    } else {
                        expanded += c;
                        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++column;
                    }
                }
                f.text = std::move(expanded);
            }
        }
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
        const Node* block = nodes.front();
        std::size_t stream = 0;
        for (std::size_t li = 0; li < lines.size(); ++li) {
            if (li > 0) ++stream;
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
                const Node* run_owner = f.node ? f.node : &owner;
                TextRun run;
                run.block = block;
                run.owner = run_owner;
                run.start = stream;
                run.text = text;
                run.x = px;
                run.y = py;
                run.w = pw;
                run.h = fh;
                run.style = f.style;
                run.clip = current_clip();
                run.selectable = style_of(*run_owner).text_view("user-select") != "none";
                if (run.selectable && m_sel_anchor.valid()) draw_selection(r, run);
                stream += text.size();
                m_runs.push_back(std::move(run));
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

    float select_content_width(fizmo::graphics::Renderer& r, const Node& node) {
        const bark::ComputedStyle& style = style_of(node);
        const Box pad = box_of(style, "padding");
        const fizmo::text::TextStyle ts = text_style(node);
        float widest = 0.0f;
        for (const SelectOption& o : select_options(node)) widest = std::max(widest, text_width(r, o.label, ts));
        if (!select_index(node)) widest = std::max(widest, text_width(r, node.has_attribute("value") ? node.attribute_or("value", "") : node.attribute_or("placeholder", ""), ts));
        const float arrow_w = text_width(r, "\xE2\x96\xBE", ts) + 4.0f;
        return std::ceil(widest + pad.left + pad.right + arrow_w + 6.0f);
    }

    std::string fit_text(fizmo::graphics::Renderer& r, const std::string& text, const fizmo::text::TextStyle& ts, float max_w) {
        if (max_w <= 0.0f) return std::string();
        if (text_width(r, text, ts) <= max_w) return text;
        const std::string dots = "\xE2\x80\xA6";
        std::vector<std::size_t> ends;
        for (std::size_t i = 1; i <= text.size(); ++i) {
            if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ends.push_back(i);
        }
        std::size_t lo = 0;
        std::size_t hi = ends.size();
        while (lo < hi) {
            const std::size_t mid = (lo + hi + 1) / 2;
            if (text_width(r, text.substr(0, ends[mid - 1]) + dots, ts) <= max_w) lo = mid;
            else hi = mid - 1;
        }
        return lo == 0 ? (text_width(r, dots, ts) <= max_w ? dots : std::string()) : text.substr(0, ends[lo - 1]) + dots;
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
        const float w = style.get("flex") ? width : box_width(style, width);
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
        std::string text = focused ? m_edit : input_text(element);
        std::size_t caret = focused ? std::min(m_edit_caret, text.size()) : text.size();
        if (element.has_attribute("masked") && !text.empty()) {
            std::size_t points = 0;
            for (unsigned char c : text) points += (c & 0xC0) != 0x80 ? 1 : 0;
            const std::size_t before = code_points_before(text, caret);
            text.clear();
            for (std::size_t i = 0; i < points; ++i) text += "\xE2\x80\xA2";
            caret = before * 3;
        }
        fizmo::text::TextStyle shown = ts;
        if (disabled) shown.set_fg_color(with_alpha(raw_text_color(element), fade));
        const std::string dots = "\xE2\x80\xA6";
        fizmo::text::TextStyle faint = shown;
        faint.set_fg_color(with_alpha(text_color(element), 0.55));
        if (text.empty() && element.has_attribute("placeholder")) {
            fizmo::text::TextStyle muted = ts;
            muted.set_fg_color(with_alpha(text_color(element), 0.45));
            push_text(tx, y + pad.top, tw, lh, fit_text(r, element.attribute_or("placeholder", ""), muted, tw), muted);
            if (focused) push_rect(DrawOp::Kind::Fill, tx, y + pad.top + 2, 2, lh - 4, accent_color(element));
        } else if (!focused) {
            push_text(tx, y + pad.top, tw, lh, fit_text(r, text, shown, tw), shown);
        } else {
            const float full = text_width(r, text, ts);
            const float dots_w = text_width(r, dots, ts);
            if (m_edit_view != &element) {
                m_edit_view = &element;
                m_edit_scroll = 0.0f;
            }
            float& scroll = m_edit_scroll;
            auto left_pad = [&] { return scroll > 0.5f ? dots_w + 2.0f : 0.0f; };
            auto right_pad = [&] { return full - scroll > tw - left_pad() + 0.5f ? dots_w + 2.0f : 0.0f; };
            if (m_edit_click_x >= 0.0f) {
                const float local = m_edit_click_x - (tx + left_pad()) + scroll;
                std::size_t best = 0;
                float best_d = std::abs(local);
                for (std::size_t at = 0; at < text.size();) {
                    at = next_code_point(text, at);
                    const float d = std::abs(text_width(r, text.substr(0, at), ts) - local);
                    if (d < best_d) {
                        best_d = d;
                        best = at;
                    }
                }
                caret = best;
                const std::size_t previous = m_edit_caret;
                m_edit_caret = element.has_attribute("masked") ? [&] {
                    std::size_t n = best / 3;
                    std::size_t i = 0;
                    while (n > 0 && i < m_edit.size()) {
                        i = next_code_point(m_edit, i);
                        --n;
                    }
                    return i;
                }() : best;
                edit_apply_click(previous);
            }
            const float caret_px = text_width(r, text.substr(0, caret), ts);
            if (full <= tw) {
                scroll = 0.0f;
            } else {
                for (int pass = 0; pass < 3; ++pass) {
                    const float view = std::max(8.0f, tw - left_pad() - right_pad() - 2.0f);
                    if (caret_px < scroll) scroll = std::max(0.0f, caret_px - view * 0.33f);
                    else if (caret_px > scroll + view) scroll = caret_px - view;
                    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, full - (tw - dots_w - 4.0f)));
                }
            }
            const float lp = left_pad();
            const float rp = right_pad();
            const float view_w = std::max(1.0f, tw - lp - rp);
            push_clip(Rect{ tx + lp, y, view_w, h });
            {
                std::size_t a = 0;
                std::size_t b = 0;
                if (!m_edit_all && !element.has_attribute("masked") && edit_range(a, b)) {
                    const float ax = text_width(r, text.substr(0, std::min(a, text.size())), ts);
                    const float bx = text_width(r, text.substr(0, std::min(b, text.size())), ts);
                    push_rect(DrawOp::Kind::Fill, tx + lp - scroll + ax, y + pad.top + 1, std::max(1.0f, bx - ax), lh - 2, with_alpha(accent_color(element), 0.35), 1, 2);
                }
            }
            push_text(tx + lp - scroll, y + pad.top, full + 4.0f, lh, text, shown);
            if (m_edit_all && !text.empty()) push_rect(DrawOp::Kind::Fill, tx + lp - scroll - 1, y + pad.top + 1, full + 2, lh - 2, with_alpha(accent_color(element), 0.35), 1, 2);
            pop_clip();
            if (lp > 0.0f) push_text(tx, y + pad.top, dots_w + 2.0f, lh, dots, faint);
            if (rp > 0.0f) push_text(tx + tw - dots_w, y + pad.top, dots_w + 2.0f, lh, dots, faint);
            const float cx = std::clamp(tx + lp + caret_px - scroll, tx + lp, tx + lp + view_w - 1.0f);
            push_rect(DrawOp::Kind::Fill, cx, y + pad.top + 2, 2, lh - 4, accent_color(element));
        }
        if (!disabled) add_hit(Hit{ &element, kind, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
        y += h;
    }

    bool math_layout_current() const {
        return m_focus && m_focus->tag() == "input_math" && m_math_layout_text == m_edit && !m_math_layout.carets.empty();
    }

    MathInputLayout math_field_layout(fizmo::graphics::Renderer& r, const std::string& text, const fizmo::text::TextStyle& ts) {
        const float lh = line_height(r, ts);
        const double base_size = ts.has_size() ? ts.size() : 16.0;
        MathInputMetrics m;
        m.ascent = lh * 0.78f;
        m.descent = lh * 0.22f;
        m.width = [&](const std::string& s, float scale, bool italic) {
            fizmo::text::TextStyle t = ts;
            t.set_size(base_size * scale);
            t.set_italic(italic);
            return text_width(r, s, t);
        };
        return layout_math_input(text, m);
    }

    void layout_math_field(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        const Box pad = box_of(style, "padding");
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        const double base_size = ts.has_size() ? ts.size() : 16.0;
        const float w = style.get("flex") ? width : box_width(style, width);
        const bool focused = m_focus == &element;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);
        const std::string text = focused ? m_edit : input_text(element);
        const std::size_t caret = focused ? std::min(m_edit_caret, text.size()) : text.size();
        const MathInputLayout layout = math_field_layout(r, text, ts);
        if (focused) {
            m_math_layout = layout;
            m_math_layout_text = text;
        }
        const float content_h = std::max(lh, layout.ascent + layout.descent);
        const float h = content_h + pad.top + pad.bottom;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));

        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, w, h, with_alpha(to_color(*bg), fade), 1, radius);
        Color border = to_color(style.color("border-color").value_or(bark::Rgba{ 255, 255, 255, 60 }));
        if (focused) border = m_invalid ? Color(243, 139, 168, 255) : accent_color(element);
        push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(border, fade), focused ? 2u : 1u, radius);

        const float tx = x + pad.left;
        const float tw = std::max(1.0f, x + w - pad.right - tx);
        const float baseline = y + pad.top + 0.5f * (content_h - layout.ascent - layout.descent) + layout.ascent;
        fizmo::text::TextStyle shown = ts;
        if (disabled) shown.set_fg_color(with_alpha(raw_text_color(element), fade));
        const Color ink = text_color(element);

        if (text.empty()) {
            if (element.has_attribute("placeholder")) {
                fizmo::text::TextStyle muted = ts;
                muted.set_fg_color(with_alpha(text_color(element), 0.45));
                push_text(tx, baseline - layout.base_ascent, tw, lh, fit_text(r, element.attribute_or("placeholder", ""), muted, tw), muted);
            }
            if (focused) push_rect(DrawOp::Kind::Fill, tx, baseline - layout.base_ascent + 2, 2, lh - 4, accent_color(element));
            if (!disabled) add_hit(Hit{ &element, InputKind::String, x, y, w, h, 0, 0 });
            add_box(ElementBox{ &element, x, y, w, h });
            y += h;
            return;
        }

        if (m_edit_view != &element && focused) {
            m_edit_view = &element;
            m_edit_scroll = 0.0f;
        }
        float scroll = focused ? m_edit_scroll : 0.0f;
        if (focused && m_edit_click_x >= 0.0f) {
            const std::size_t previous = m_edit_caret;
            m_edit_caret = math_input_hit(layout, m_edit_click_x - tx + scroll, m_edit_click_y - baseline);
            edit_apply_click(previous);
        }
        const std::size_t shown_caret = focused ? std::min(m_edit_caret, text.size()) : caret;
        const MathInputCaret& cpos = math_input_caret(layout, shown_caret);
        if (focused) {
            if (layout.width <= tw - 2.0f) {
                scroll = 0.0f;
            } else {
                if (cpos.x < scroll) scroll = std::max(0.0f, cpos.x - tw * 0.33f);
                else if (cpos.x > scroll + tw - 3.0f) scroll = cpos.x - tw + 3.0f;
                scroll = std::clamp(scroll, 0.0f, std::max(0.0f, layout.width - tw + 3.0f));
            }
            m_edit_scroll = scroll;
        }
        const float ox = tx - scroll;
        push_clip(Rect{ tx - 1.0f, y + 1.0f, tw + 2.0f, h - 2.0f });
        {
            std::size_t a = 0;
            std::size_t b = 0;
            if (focused && edit_range(a, b)) {
                const Color sel = with_alpha(accent_color(element), 0.35);
                for (const MathInputGlyph& g : layout.glyphs) {
                    if (!g.source || g.begin < a || g.end > b) continue;
                    const float gw = std::max(g.width, g.visible ? 1.0f : 0.0f);
                    if (gw <= 0.0f) continue;
                    push_rect(DrawOp::Kind::Fill, ox + g.x, baseline + g.y - layout.base_ascent * g.scale, gw, lh * g.scale, sel, 1, 0);
                }
            }
        }
        for (const MathInputGlyph& g : layout.glyphs) {
            if (!g.visible || g.text.empty()) continue;
            fizmo::text::TextStyle gs = shown;
            gs.set_size(base_size * g.scale);
            gs.set_italic(g.italic);
            push_text(ox + g.x + g.pad, baseline + g.y - layout.base_ascent * g.scale, g.width + 4.0f, lh * g.scale + 2.0f, g.text, gs);
        }
        for (const MathInputLine& l : layout.lines) {
            const float th = std::max(1.0f, l.thickness);
            if (std::fabs(l.y1 - l.y2) < 0.01f) {
                push_rect(DrawOp::Kind::Fill, ox + std::min(l.x1, l.x2), baseline + l.y1 - 0.5f * th, std::fabs(l.x2 - l.x1), th, with_alpha(ink, fade), 1, 0);
            } else {
                DrawOp& op = push(DrawOp::Kind::Line, ox + l.x1, baseline + l.y1, l.x2 - l.x1, l.y2 - l.y1, with_alpha(ink, fade));
                op.stroke = static_cast<unsigned>(std::max(1.0f, std::round(th)));
            }
        }
        pop_clip();
        if (focused && !m_edit_all) {
            const float ch = lh * cpos.scale;
            const float cx = std::clamp(ox + cpos.x, tx, tx + tw - 1.0f);
            push_rect(DrawOp::Kind::Fill, cx, baseline + cpos.y - layout.base_ascent * cpos.scale + 1.0f, 2, std::max(4.0f, ch - 2.0f), accent_color(element));
        }
        if (focused && m_edit_all) push_rect(DrawOp::Kind::Fill, ox - 1, baseline - layout.ascent, layout.width + 2, layout.ascent + layout.descent, with_alpha(accent_color(element), 0.3), 1, 2);
        if (!disabled) add_hit(Hit{ &element, InputKind::String, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
        y += h;
    }

    void layout_select(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width) {
        const bark::ComputedStyle& style = style_of(element);
        const Box pad = box_of(style, "padding");
        const fizmo::text::TextStyle ts = text_style(element);
        const float lh = line_height(r, ts);
        float w = box_width(style, width);
        if (!resolve_length(style, "width", width)) {
            float content = select_content_width(r, element);
            if (const auto mn = resolve_length(style, "min-width", width)) content = std::max(content, *mn);
            if (const auto mx = resolve_length(style, "max-width", width)) content = std::min(content, *mx);
            w = std::min(width, content);
        }
        const float h = lh + pad.top + pad.bottom;
        const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
        const bool open = m_menu.node == &element;
        const bool focused = m_focused == &element || open;
        const bool disabled = is_disabled(&element);
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);

        if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, w, h, with_alpha(to_color(*bg), fade), 1, radius);
        Color border = to_color(style.color("border-color").value_or(bark::Rgba{ 255, 255, 255, 60 }));
        if (focused) border = accent_color(element);
        push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(border, fade), focused ? 2u : 1u, radius);

        const std::vector<SelectOption> options = select_options(element);
        const std::optional<std::size_t> index = select_index(element);
        const float arrow_w = text_width(r, "\xE2\x96\xBE", ts) + 4.0f;
        const float tx = x + pad.left;
        const float tw = std::max(1.0f, x + w - pad.right - arrow_w - tx);
        fizmo::text::TextStyle shown = ts;
        if (disabled) shown.set_fg_color(with_alpha(raw_text_color(element), fade));
        push_clip(Rect{ x, y, std::max(0.0f, x + w - pad.right - arrow_w - x), h });
        if (index && *index < options.size()) {
            push_text(tx, y + pad.top, tw, lh, fit_text(r, options[*index].label, shown, tw), shown);
        } else {
            fizmo::text::TextStyle muted = ts;
            muted.set_fg_color(with_alpha(text_color(element), 0.45));
            push_text(tx, y + pad.top, tw, lh, fit_text(r, element.has_attribute("value") ? element.attribute_or("value", "") : element.attribute_or("placeholder", ""), muted, tw), muted);
        }
        pop_clip();
        fizmo::text::TextStyle arrow = ts;
        arrow.set_fg_color(with_alpha(text_color(element), disabled ? 0.3 : 0.7));
        push_text(x + w - pad.right - arrow_w + 4.0f, y + pad.top, arrow_w, lh, "\xE2\x96\xBE", arrow);
        if (open) {
            m_menu.anchor = Rect{ x, y, w, h };
            m_menu.anchored = true;
        }
        if (!disabled) add_hit(Hit{ &element, InputKind::Select, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
        y += h;
    }

    void open_menu(const Node* node) {
        m_menu = SelectMenu{};
        m_menu.node = node;
        m_menu.active = select_index(*node).value_or(0);
        m_menu.reveal = true;
    }

    void close_menu() {
        m_menu = SelectMenu{};
    }

    void choose_option(std::size_t index) {
        if (!m_menu.node && !m_focused) return;
        const Node* target = m_menu.node ? m_menu.node : m_focused;
        const std::vector<SelectOption> options = select_options(*target);
        if (index >= options.size() || options[index].disabled) return;
        Node& node = mutable_node(*target);
        const bool changed = input_string(node).value_or("") != options[index].value || !node.has_attribute("value");
        set_input_string(document(), node, options[index].value);
        if (changed) fire_value(node, true);
    }

    std::size_t step_option(const Node& node, std::size_t from, int direction) {
        const std::vector<SelectOption> options = select_options(node);
        if (options.empty()) return from;
        std::size_t at = std::min(from, options.size() - 1);
        for (std::size_t tries = 0; tries < options.size(); ++tries) {
            if (direction < 0 && at == 0) return from;
            if (direction > 0 && at + 1 >= options.size()) return from;
            at = direction < 0 ? at - 1 : at + 1;
            if (!options[at].disabled) return at;
        }
        return from;
    }

    void layout_menu(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        SelectMenu& m = m_menu;
        if (!m.node) return;
        if (!m.anchored) {
            m = SelectMenu{};
            return;
        }
        const std::vector<SelectOption> options = select_options(*m.node);
        const fizmo::text::TextStyle ts = text_style(*m.node);
        const Color fg = raw_text_color(*m.node);
        const Color accent = accent_color(*m.node);
        const float lh = line_height(r, ts);
        m.row_h = lh + 8.0f;
        float widest = m.anchor.w;
        for (const SelectOption& o : options) widest = std::max(widest, text_width(r, o.label, ts) + 28.0f);
        const float panel_w = std::min(widest, std::max(m.anchor.w, view_w - 8.0f));
        const float below = view_h - (m.anchor.y + m.anchor.h) - 8.0f;
        const float above = m.anchor.y - 8.0f;
        const float full = m.row_h * static_cast<float>(options.size()) + 8.0f;
        const float limit = std::min(m.row_h * 12.0f + 8.0f, std::max(below, above));
        const float panel_h = std::max(m.row_h + 8.0f, std::min(full, limit));
        float px = std::clamp(m.anchor.x, 4.0f, std::max(4.0f, view_w - panel_w - 4.0f));
        float py = full <= below || below >= above ? m.anchor.y + m.anchor.h + 4.0f : m.anchor.y - 4.0f - panel_h;
        py = std::clamp(py, 4.0f, std::max(4.0f, view_h - panel_h - 4.0f));
        m.panel = Rect{ px, py, panel_w, panel_h };
        m.max_scroll = std::max(0.0f, full - panel_h);
        if (m.reveal) {
            const float top = static_cast<float>(m.active) * m.row_h;
            if (top < m.scroll) m.scroll = top;
            if (top + m.row_h > m.scroll + panel_h - 8.0f) m.scroll = top + m.row_h - (panel_h - 8.0f);
            m.reveal = false;
        }
        m.scroll = std::clamp(m.scroll, 0.0f, m.max_scroll);

        const Color window_bg = to_color(style_of(m_window).color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
        const Color panel_bg = mix(window_bg, fg, 0.06f);
        push_rect(DrawOp::Kind::Fill, px + 2, py + 3, panel_w, panel_h, Color(0, 0, 0, 70), 1, 6);
        push_rect(DrawOp::Kind::Fill, px, py, panel_w, panel_h, panel_bg, 1, 6);
        push_rect(DrawOp::Kind::Stroke, px, py, panel_w, panel_h, with_alpha(fg, 0.22), 1, 6);
        push_clip(Rect{ px, py + 4, panel_w, panel_h - 8 });
        m.rows.clear();
        const std::optional<std::size_t> chosen = select_index(*m.node);
        for (std::size_t i = 0; i < options.size(); ++i) {
            const Rect row{ px + 4, py + 4 + static_cast<float>(i) * m.row_h - m.scroll, panel_w - 8, m.row_h };
            if (row.y + row.h < py || row.y > py + panel_h) continue;
            const bool hovered = row.contains(m_mouse_x, m_mouse_y) && m.panel.contains(m_mouse_x, m_mouse_y);
            if (chosen && *chosen == i) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(accent, 0.25), 1, 4);
            if (i == m.active || hovered) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(fg, 0.08), 1, 4);
            fizmo::text::TextStyle text = ts;
            text.set_fg_color(with_alpha(fg, options[i].disabled ? 0.4 : 0.95));
            push_text(row.x + 10, row.y + 4, row.w - 14, lh, options[i].label, text);
            m.rows.emplace_back(row, i);
        }
        pop_clip();
        if (m.max_scroll > 0.0f) {
            const float track = panel_h - 8.0f;
            const float thumb = std::max(20.0f, track * track / (track + m.max_scroll));
            push_rect(DrawOp::Kind::Fill, px + panel_w - 6, py + 4 + (track - thumb) * (m.scroll / m.max_scroll), 3, thumb, with_alpha(fg, 0.3), 1, 2);
        }
    }

    bool context_selectable(std::size_t i) const { return i < m_context.items.size() && !m_context.items[i].separator && !m_context.items[i].disabled; }

    void step_context(int direction) {
        const std::size_t count = m_context.items.size();
        if (count == 0) return;
        std::size_t at = m_context.active;
        for (std::size_t tries = 0; tries < count; ++tries) {
            if (at >= count) at = direction > 0 ? 0 : count - 1;
            else at = direction > 0 ? (at + 1) % count : (at + count - 1) % count;
            if (context_selectable(at)) {
                m_context.active = at;
                return;
            }
        }
    }

    void choose_context(std::size_t index) {
        if (!context_selectable(index) || !m_context.owner) return;
        const Node* owner = m_context.owner;
        const std::string value = m_context.items[index].value;
        m_context = ContextMenu{};
        poke();
        if (!document().is_attached(*owner)) return;
        if (value.rfind("grove:", 0) == 0 && m_focus == owner) {
            if (value == "grove:cut") edit_copy(true);
            else if (value == "grove:copy") edit_copy(false);
            else if (value == "grove:paste") edit_paste();
            else if (value == "grove:select-all") m_edit_all = !m_edit.empty();
            return;
        }
        Event e;
        e.type = "menu";
        e.key = value;
        e.bubbles = false;
        dispatch(owner, e);
    }

    void layout_context(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        ContextMenu& m = m_context;
        if (!m.owner) return;
        if (!document().is_attached(*m.owner)) {
            m = ContextMenu{};
            return;
        }
        const fizmo::text::TextStyle ts = text_style(m_window);
        const Color fg = raw_text_color(m_window);
        const Color accent = accent_color(m_window);
        const float lh = line_height(r, ts);
        m.row_h = lh + 8.0f;
        const float sep_h = 9.0f;
        fizmo::text::TextStyle muted = ts;
        muted.set_fg_color(with_alpha(fg, 0.55));
        float label_w = 0;
        float detail_w = 0;
        float full = 8.0f;
        for (const MenuItem& item : m.items) {
            if (item.separator) {
                full += sep_h;
                continue;
            }
            full += m.row_h;
            label_w = std::max(label_w, text_width(r, item.label, ts));
            if (!item.detail.empty()) detail_w = std::max(detail_w, text_width(r, item.detail, muted));
        }
        const float panel_w = std::min(std::max(160.0f, label_w + (detail_w > 0 ? detail_w + 36.0f : 0.0f) + 32.0f), std::max(160.0f, view_w - 8.0f));
        const float panel_h = std::min(full, std::max(m.row_h + 8.0f, view_h - 8.0f));
        float px = m.x + 2.0f;
        float py = m.y + 2.0f;
        if (px + panel_w > view_w - 4.0f) px = std::max(4.0f, m.x - panel_w - 2.0f);
        if (py + panel_h > view_h - 4.0f) py = std::max(4.0f, view_h - panel_h - 4.0f);
        m.panel = Rect{ px, py, panel_w, panel_h };
        const Color window_bg = to_color(style_of(m_window).color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
        const Color panel_bg = mix(window_bg, fg, 0.06f);
        push_rect(DrawOp::Kind::Fill, px + 2, py + 3, panel_w, panel_h, Color(0, 0, 0, 70), 1, 6);
        push_rect(DrawOp::Kind::Fill, px, py, panel_w, panel_h, panel_bg, 1, 6);
        push_rect(DrawOp::Kind::Stroke, px, py, panel_w, panel_h, with_alpha(fg, 0.22), 1, 6);
        push_clip(Rect{ px, py + 2, panel_w, panel_h - 4 });
        m.rows.clear();
        float ry = py + 4.0f;
        for (std::size_t i = 0; i < m.items.size(); ++i) {
            const MenuItem& item = m.items[i];
            if (item.separator) {
                push_rect(DrawOp::Kind::Fill, px + 8, ry + 4, panel_w - 16, 1, with_alpha(fg, 0.15));
                ry += sep_h;
                continue;
            }
            const Rect row{ px + 4, ry, panel_w - 8, m.row_h };
            const bool hovered = row.contains(m_mouse_x, m_mouse_y);
            if (hovered && context_selectable(i)) m.active = i;
            if (i == m.active && context_selectable(i)) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(accent, 0.28), 1, 4);
            fizmo::text::TextStyle text = ts;
            text.set_fg_color(with_alpha(fg, item.disabled ? 0.4 : 0.95));
            push_text(row.x + 12, row.y + 4, row.w - 24, lh, item.label, text);
            if (!item.detail.empty()) {
                const float dw = text_width(r, item.detail, muted);
                push_text(row.x + row.w - 12 - dw, row.y + 4, dw + 4, lh, item.detail, muted);
            }
            m.rows.emplace_back(row, i);
            ry += m.row_h;
        }
        pop_clip();
    }

    static Color term_color(const TermRgb& c) { return Color(c.r, c.g, c.b, 255); }

    void term_box(char32_t ch, float x, float y, float w, float h, const Color& color) {
        const float cx = std::floor(x + w / 2.0f);
        const float cy = std::floor(y + h / 2.0f);
        const float light = std::max(1.0f, std::round(w / 8.0f));
        const float heavy = std::max(2.0f, light * 2.0f);
        if (ch >= 0x2500 && ch <= 0x257F) {
            const std::uint8_t arms = k_box_arms[ch - 0x2500];
            auto arm = [&](int weight, int side) {
                if (weight == 0) return;
                const float t = weight == 2 ? heavy : light;
                const std::vector<float> offsets = weight == 3 ? std::vector<float>{ -light - 1.0f, light + 1.0f } : std::vector<float>{ 0.0f };
                for (float o : offsets) {
                    if (side == 0) push_rect(DrawOp::Kind::Fill, x, cy - t / 2.0f + o, cx - x + t / 2.0f, t, color);
                    else if (side == 1) push_rect(DrawOp::Kind::Fill, cx - t / 2.0f, cy - t / 2.0f + o, x + w - cx + t / 2.0f, t, color);
                    else if (side == 2) push_rect(DrawOp::Kind::Fill, cx - t / 2.0f + o, y, t, cy - y + t / 2.0f, color);
                    else push_rect(DrawOp::Kind::Fill, cx - t / 2.0f + o, cy - t / 2.0f, t, y + h - cy + t / 2.0f, color);
                }
            };
            arm(arms & 3, 0);
            arm((arms >> 2) & 3, 1);
            arm((arms >> 4) & 3, 2);
            arm((arms >> 6) & 3, 3);
            return;
        }
        auto fill = [&](float fx, float fy, float fw, float fh) { push_rect(DrawOp::Kind::Fill, std::round(x + fx * w), std::round(y + fy * h), std::round(fw * w) + 0.5f, std::round(fh * h) + 0.5f, color); };
        if (ch == 0x2580) fill(0, 0, 1, 0.5f);
        else if (ch >= 0x2581 && ch <= 0x2587) {
            const float part = static_cast<float>(ch - 0x2580) / 8.0f;
            fill(0, 1.0f - part, 1, part);
        } else if (ch == 0x2588) fill(0, 0, 1, 1);
        else if (ch >= 0x2589 && ch <= 0x258F) fill(0, 0, static_cast<float>(0x2590 - ch) / 8.0f, 1);
        else if (ch == 0x2590) fill(0.5f, 0, 0.5f, 1);
        else if (ch >= 0x2591 && ch <= 0x2593) push_rect(DrawOp::Kind::Fill, x, y, w, h, with_alpha(color, 0.25 * static_cast<double>(ch - 0x2590)));
        else if (ch == 0x2594) fill(0, 0, 1, 0.125f);
        else if (ch == 0x2595) fill(0.875f, 0, 0.125f, 1);
        else {
            const unsigned quads[] = { 0x4, 0x8, 0x1, 0xD, 0x9, 0x7, 0xB, 0x2, 0x6, 0xE };
            const unsigned q = ch >= 0x2596 && ch <= 0x259F ? quads[ch - 0x2596] : 0;
            if (q & 1) fill(0, 0, 0.5f, 0.5f);
            if (q & 2) fill(0.5f, 0, 0.5f, 0.5f);
            if (q & 4) fill(0, 0.5f, 0.5f, 0.5f);
            if (q & 8) fill(0.5f, 0.5f, 0.5f, 0.5f);
        }
    }

    static bool term_drawn_box(char32_t ch) {
        if (ch >= 0x2500 && ch <= 0x257F) return k_box_arms[ch - 0x2500] != 0;
        return ch >= 0x2580 && ch <= 0x259F;
    }

    void layout_terminal(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, float height) {
        const bark::ComputedStyle& style = style_of(element);
        const std::shared_ptr<Terminal> term = terminal_of(document(), element);
        fizmo::text::TextStyle base = text_style(element);
        if (!style.get("font-family")) base.set_font(fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono));
        base.set_bold(false);
        base.set_italic(false);
        base.set_underline(false);
        base.set_strikethrough(false);
        const float glyph_h = line_height(r, base);
        float cell_h = std::round(glyph_h * 1.15f);
        if (const bark::ValueList* lv = style.get("line-height"); lv && !lv->empty() && (*lv)[0].is_number()) {
            if ((*lv)[0].unit.empty()) cell_h = std::round(glyph_h * static_cast<float>((*lv)[0].number));
            else if (const auto px = style.length("line-height")) cell_h = std::round(static_cast<float>(*px));
        }
        cell_h = std::max(4.0f, cell_h);
        const float cw = std::max(1.0f, text_width(r, "MMMMMMMMMMMMMMMMMMMM", base) / 20.0f);
        float h = height;
        if (h < 0.0f) {
            const std::optional<std::int64_t> rows_attr = parse_integer(element.attribute_or("rows", ""));
            h = rows_attr && *rows_attr > 0 ? static_cast<float>(*rows_attr) * cell_h : resolve_length(style, "height", m_avail_h).value_or(cell_h * 24.0f);
        }
        const int cols = std::max(2, static_cast<int>(std::floor(width / cw)));
        const int rows = std::max(1, static_cast<int>(std::floor(h / cell_h)));
        term->resize(cols, rows);

        const Color style_fg = raw_text_color(element);
        const Color style_bg = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 30, 255 }));
        const Color style_cursor = to_color(style.color("caret-color").value_or(bark::Rgba{ style_fg.red(), style_fg.green(), style_fg.blue(), 255 }));
        const std::array<std::uint8_t, 9> key{ { style_fg.red(), style_fg.green(), style_fg.blue(), style_bg.red(), style_bg.green(), style_bg.blue(), style_cursor.red(), style_cursor.green(), style_cursor.blue() } };
        auto known = m_term_colors.find(&element);
        if (known == m_term_colors.end() || known->second != key) {
            term->set_default_colors(TermRgb{ key[0], key[1], key[2] }, TermRgb{ key[3], key[4], key[5] }, TermRgb{ key[6], key[7], key[8] });
            m_term_colors[&element] = key;
        }

        const bool focused = m_focused == &element;
        const Color accent = accent_color(element);
        const Color default_bg = term_color(term->reverse_video() ? term->default_foreground() : term->default_background());
        push_rect(DrawOp::Kind::Fill, x, y, std::max(width, static_cast<float>(cols) * cw), h, default_bg);
        push_clip(Rect{ x, y, std::max(width, static_cast<float>(cols) * cw), h });
        const float text_dy = std::floor((cell_h - glyph_h) / 2.0f);
        TermCell empty;

        struct Look {
            Color fg;
            Color bg;
            bool bg_default = true;
        };
        auto look_of = [&](const TermCell& c, long long line, int col) {
            Look out;
            TermColor f = c.fg;
            if ((c.attrs & TermBold) && f.kind == TermColor::Kind::Indexed && f.index < 8) f = TermColor::indexed(f.index + 8);
            TermRgb default_fg = term->default_foreground();
            TermRgb default_bg = term->default_background();
            if (term->reverse_video()) std::swap(default_fg, default_bg);
            TermRgb fr = f.kind == TermColor::Kind::Default ? default_fg : term->resolve(f, true);
            TermRgb br = c.bg.kind == TermColor::Kind::Default ? default_bg : term->resolve(c.bg, false);
            out.bg_default = c.bg.kind == TermColor::Kind::Default;
            const bool inverse = (c.attrs & TermInverse) != 0;
            if (inverse) {
                std::swap(fr, br);
                out.bg_default = false;
            }
            if (c.attrs & TermHidden) fr = br;
            out.fg = term_color(fr);
            out.bg = term_color(br);
            if (c.attrs & TermDim) out.fg = mix(out.fg, out.bg, 0.45f);
            if (term->is_selected(line, col)) {
                out.bg = mix(out.bg, accent, 0.45f);
                out.bg_default = false;
            }
            return out;
        };

        for (int row = 0; row < rows; ++row) {
            const TermLine& line = term->view_line(row);
            const long long abs = term->view_line_number(row);
            const float ry = y + static_cast<float>(row) * cell_h;
            auto cell_at = [&](int col) -> const TermCell& { return col < static_cast<int>(line.cells.size()) ? line.cells[static_cast<std::size_t>(col)] : empty; };
            int run_start = -1;
            Color run_bg;
            for (int col = 0; col <= cols; ++col) {
                bool has = false;
                Color bg;
                if (col < cols) {
                    const Look lk = look_of(cell_at(col), abs, col);
                    has = !lk.bg_default;
                    bg = lk.bg;
                }
                if (run_start >= 0 && (!has || bg != run_bg)) {
                    push_rect(DrawOp::Kind::Fill, x + static_cast<float>(run_start) * cw, ry, static_cast<float>(col - run_start) * cw, cell_h, run_bg);
                    run_start = -1;
                }
                if (has && run_start < 0) {
                    run_start = col;
                    run_bg = bg;
                }
            }
            std::string run;
            int run_col = 0;
            fizmo::text::TextStyle run_style = base;
            Color run_fg;
            std::uint16_t run_attrs = 0;
            auto flush = [&]() {
                if (run.empty()) return;
                push_text(x + static_cast<float>(run_col) * cw, ry + text_dy, static_cast<float>(run.size()) * cw + cw, glyph_h, run, run_style);
                run.clear();
            };
            for (int col = 0; col < cols; ++col) {
                const TermCell& c = cell_at(col);
                if (c.width == 0) continue;
                const Look lk = look_of(c, abs, col);
                const float cx = x + static_cast<float>(col) * cw;
                const float cwidth = static_cast<float>(std::max<int>(1, c.width)) * cw;
                if (c.attrs & (TermUnderline | TermDoubleUnderline | TermCurlyUnderline)) {
                    const float t = std::max(1.0f, std::round(cell_h / 14.0f));
                    const float uy = ry + cell_h - t - 1.0f;
                    push_rect(DrawOp::Kind::Fill, cx, uy, cwidth, t, lk.fg);
                    if (c.attrs & TermDoubleUnderline) push_rect(DrawOp::Kind::Fill, cx, uy - t - 1.0f, cwidth, t, lk.fg);
                    if (c.attrs & TermCurlyUnderline) {
                        for (float dx = 0; dx < cwidth; dx += 4.0f) push_rect(DrawOp::Kind::Fill, cx + dx, uy - (static_cast<int>(dx / 4.0f) % 2 ? t : 0.0f), std::min(4.0f, cwidth - dx), t, lk.fg);
                    }
                }
                if (c.attrs & TermStrike) push_rect(DrawOp::Kind::Fill, cx, ry + std::round(cell_h / 2.0f), cwidth, std::max(1.0f, std::round(cell_h / 14.0f)), lk.fg);
                if (c.attrs & TermOverline) push_rect(DrawOp::Kind::Fill, cx, ry, cwidth, std::max(1.0f, std::round(cell_h / 14.0f)), lk.fg);
                if (c.ch == U' ' && c.marks.empty()) {
                    flush();
                    continue;
                }
                if (term_drawn_box(c.ch)) {
                    flush();
                    term_box(c.ch, cx, ry, cwidth, cell_h, lk.fg);
                    continue;
                }
                const bool ascii = c.ch < 0x80 && c.marks.empty() && c.width == 1;
                const std::uint16_t attrs = c.attrs & (TermBold | TermItalic);
                if (!run.empty() && (!ascii || lk.fg != run_fg || attrs != run_attrs || col != run_col + static_cast<int>(run.size()))) flush();
                fizmo::text::TextStyle st = base;
                st.set_fg_color(lk.fg);
                if (c.attrs & TermBold) st.set_bold(true);
                if (c.attrs & TermItalic) st.set_italic(true);
                if (ascii) {
                    if (run.empty()) {
                        run_col = col;
                        run_style = st;
                        run_fg = lk.fg;
                        run_attrs = attrs;
                    }
                    run += static_cast<char>(c.ch);
                    continue;
                }
                std::string glyph;
                auto put = [&glyph](char32_t cp) {
                    if (cp < 0x80) glyph += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        glyph += static_cast<char>(0xC0 | (cp >> 6));
                        glyph += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        glyph += static_cast<char>(0xE0 | (cp >> 12));
                        glyph += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        glyph += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        glyph += static_cast<char>(0xF0 | (cp >> 18));
                        glyph += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        glyph += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        glyph += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                };
                put(c.ch);
                for (char32_t m : c.marks) put(m);
                push_text(cx, ry + text_dy, cwidth + cw, glyph_h, glyph, st);
            }
            flush();
        }

        if (term->view_offset() == 0 && term->cursor_visible()) {
            const int ccol = std::clamp(term->cursor_column(), 0, cols - 1);
            const int crow = std::clamp(term->cursor_row(), 0, rows - 1);
            const TermCell& c = term->line(crow).cells.empty() ? empty : term->line(crow).cells[static_cast<std::size_t>(ccol)];
            const float cx = x + static_cast<float>(ccol) * cw;
            const float cy = y + static_cast<float>(crow) * cell_h;
            const float cwidth = static_cast<float>(c.width == 2 ? 2 : 1) * cw;
            const Color cursor = term_color(term->cursor_color());
            if (!focused) {
                push_rect(DrawOp::Kind::Stroke, cx, cy, cwidth, cell_h, cursor);
            } else if (term->cursor_shape() == TermCursorShape::Block) {
                push_rect(DrawOp::Kind::Fill, cx, cy, cwidth, cell_h, cursor);
                if (c.ch != U' ' && c.width != 0) {
                    fizmo::text::TextStyle st = base;
                    const Color under = default_bg;
                    st.set_fg_color(under);
                    if (c.attrs & TermBold) st.set_bold(true);
                    if (term_drawn_box(c.ch)) term_box(c.ch, cx, cy, cwidth, cell_h, under);
                    else {
                        std::string glyph;
                        const char32_t cp = c.ch;
                        if (cp < 0x80) glyph += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            glyph += static_cast<char>(0xC0 | (cp >> 6));
                            glyph += static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            glyph += static_cast<char>(0xE0 | (cp >> 12));
                            glyph += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            glyph += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            glyph += static_cast<char>(0xF0 | (cp >> 18));
                            glyph += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            glyph += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            glyph += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        push_text(cx, cy + text_dy, cwidth + cw, glyph_h, glyph, st);
                    }
                }
            } else if (term->cursor_shape() == TermCursorShape::Underline) {
                const float t = std::max(2.0f, std::round(cell_h / 8.0f));
                push_rect(DrawOp::Kind::Fill, cx, cy + cell_h - t, cwidth, t, cursor);
            } else {
                push_rect(DrawOp::Kind::Fill, cx, cy, std::max(2.0f, std::round(cw / 6.0f)), cell_h, cursor);
            }
        }
        if (term->view_offset() > 0 && term->scrollback_size() > 0) {
            const float total = static_cast<float>(term->scrollback_size()) + static_cast<float>(rows);
            const float thumb = std::max(16.0f, h * static_cast<float>(rows) / total);
            const float at = (static_cast<float>(term->scrollback_size() - static_cast<std::size_t>(term->view_offset())) / std::max(1.0f, static_cast<float>(term->scrollback_size()))) * (h - thumb);
            push_rect(DrawOp::Kind::Fill, x + static_cast<float>(cols) * cw - 4.0f, y + at, 3.0f, thumb, with_alpha(style_fg, 0.45));
        }
        pop_clip();
        m_term_views.push_back(TermView{ &element, x, y, cw, cell_h, cols, rows });
        y += h;
    }

    bool over_popup(float px, float py) const {
        if (m_menu.node && m_menu.panel.contains(px, py)) return true;
        if (m_picker.node && m_picker.panel.contains(px, py)) return true;
        if (m_context.owner && m_context.panel.contains(px, py)) return true;
        return false;
    }

    TermView* term_view_at(float px, float py) {
        if (over_popup(px, py)) return nullptr;
        for (auto it = m_term_views.rbegin(); it != m_term_views.rend(); ++it) {
            if (it->contains(px, py) && it->node && document().is_attached(*it->node) && current_clip_contains(it->node, px, py)) return &*it;
        }
        return nullptr;
    }

    bool current_clip_contains(const Node* node, float px, float py) {
        const Node* hit = hit_element(px, py);
        return hit && (hit == node || hit->is_inside(node));
    }

    TermView* term_view_of(const Node* node) {
        for (auto it = m_term_views.rbegin(); it != m_term_views.rend(); ++it) {
            if (it->node == node) return &*it;
        }
        return nullptr;
    }

    void term_send(const Node& node, const std::string& bytes) {
        if (bytes.empty()) return;
        const std::shared_ptr<Terminal> term = terminal_of(document(), node);
        term->reset_view();
        term->clear_selection();
        term->send(bytes);
        poke();
    }

    void term_copy(const Node& node) {
        const std::shared_ptr<Terminal> term = terminal_of(document(), node);
        if (term->has_selection() && m_native) put_clipboard(m_native, term->selected_text());
    }

    void term_paste(const Node& node) {
        if (!m_native) return;
        const std::string text = read_clipboard(m_native);
        if (!text.empty()) term_send(node, terminal_of(document(), node)->encode_paste(text));
    }

    static char32_t term_key_char(fizmo::input::Key k, bool shift) {
        using fizmo::input::Key;
        if (k >= Key::A && k <= Key::Z) return static_cast<char32_t>((shift ? 'A' : 'a') + (static_cast<int>(k) - static_cast<int>(Key::A)));
        if (k >= Key::Num0 && k <= Key::Num9) return static_cast<char32_t>('0' + (static_cast<int>(k) - static_cast<int>(Key::Num0)));
        switch (k) {
            case Key::Space: return U' ';
            case Key::Minus: return shift ? U'_' : U'-';
            case Key::Equals: return shift ? U'+' : U'=';
            case Key::LeftBracket: return shift ? U'{' : U'[';
            case Key::RightBracket: return shift ? U'}' : U']';
            case Key::Backslash: return shift ? U'|' : U'\\';
            case Key::Semicolon: return shift ? U':' : U';';
            case Key::Quote: return shift ? U'"' : U'\'';
            case Key::Grave: return shift ? U'~' : U'`';
            case Key::Comma: return shift ? U'<' : U',';
            case Key::Period: return shift ? U'>' : U'.';
            case Key::Slash: return shift ? U'?' : U'/';
            default: return 0;
        }
    }

    static TermKey term_key_of(fizmo::input::Key k) {
        using fizmo::input::Key;
        switch (k) {
            case Key::Up: return TermKey::Up;
            case Key::Down: return TermKey::Down;
            case Key::Left: return TermKey::Left;
            case Key::Right: return TermKey::Right;
            case Key::Home: return TermKey::Home;
            case Key::End: return TermKey::End;
            case Key::PageUp: return TermKey::PageUp;
            case Key::PageDown: return TermKey::PageDown;
            case Key::Insert: return TermKey::Insert;
            case Key::Delete: return TermKey::Delete;
            case Key::Backspace: return TermKey::Backspace;
            case Key::Tab: return TermKey::Tab;
            case Key::Enter: return TermKey::Enter;
            case Key::NumpadEnter: return TermKey::KeypadEnter;
            case Key::Escape: return TermKey::Escape;
            case Key::F1: return TermKey::F1;
            case Key::F2: return TermKey::F2;
            case Key::F3: return TermKey::F3;
            case Key::F4: return TermKey::F4;
            case Key::F5: return TermKey::F5;
            case Key::F6: return TermKey::F6;
            case Key::F7: return TermKey::F7;
            case Key::F8: return TermKey::F8;
            case Key::F9: return TermKey::F9;
            case Key::F10: return TermKey::F10;
            case Key::F11: return TermKey::F11;
            case Key::F12: return TermKey::F12;
            default: return TermKey::None;
        }
    }

    void terminal_key(const Node& node, const fizmo::windows::WindowEvent& e) {
        using fizmo::input::Key;
        using fizmo::input::Modifiers;
        const Key k = e.logical_key != Key::Unknown ? e.logical_key : e.physical_key;
        const bool shift = fizmo::input::has(e.mods, Modifiers::Shift);
        const bool ctrl = fizmo::input::has(e.mods, Modifiers::Control);
        const bool alt = fizmo::input::has(e.mods, Modifiers::Alt);
        const bool altgr = fizmo::input::has(e.mods, Modifiers::AltGr);
        m_term_swallow = false;
        const std::shared_ptr<Terminal> term = terminal_of(document(), node);
        if (ctrl && shift && k == Key::C) {
            term_copy(node);
            return;
        }
        if ((ctrl && shift && k == Key::V) || (shift && !ctrl && k == Key::Insert)) {
            term_paste(node);
            return;
        }
        if (ctrl && !shift && !alt && k == Key::C && term->has_selection() && node.attribute_or("copy-on-ctrl-c", "true") != "false") {
            term_copy(node);
            term->clear_selection();
            poke();
            return;
        }
        if (shift && !ctrl && !alt && (k == Key::PageUp || k == Key::PageDown) && !term->alternate_screen()) {
            term->scroll_view(k == Key::PageUp ? term->rows() - 1 : -(term->rows() - 1));
            poke();
            return;
        }
        if (shift && ctrl && !alt && (k == Key::Home || k == Key::End) && !term->alternate_screen()) {
            if (k == Key::Home) term->scroll_view(static_cast<int>(term->scrollback_size()));
            else term->reset_view();
            poke();
            return;
        }
        unsigned mods = 0;
        if (shift) mods |= TermShift;
        if (alt && !altgr) mods |= TermAlt;
        if (ctrl && !altgr) mods |= TermCtrl;
        const TermKey tk = term_key_of(k);
        std::string bytes;
        if (tk != TermKey::None) {
            bytes = term->encode_key(tk, mods);
        } else if ((mods & (TermCtrl | TermAlt)) != 0) {
            const char32_t c = term_key_char(k, shift);
            if (c != 0) {
                bytes = term->encode_char(c, mods & (TermCtrl | TermAlt));
                if ((mods & TermAlt) && !(mods & TermCtrl)) m_term_swallow = true;
            }
        }
        term_send(node, bytes);
    }

    TermPoint term_point(const TermView& v, const Terminal& term, float px, float py, bool boundary) const {
        const int row = std::clamp(static_cast<int>(std::floor((py - v.y) / v.cell_h)), 0, v.rows - 1);
        const float fx = (px - v.x) / v.cell_w;
        const int col = std::clamp(static_cast<int>(boundary ? std::floor(fx + 0.5f) : std::floor(fx)), 0, boundary ? v.cols : v.cols - 1);
        return TermPoint{ term.view_line_number(row), col };
    }

    bool term_mouse_down(float px, float py, int button) {
        if (m_modal || m_menu.node || m_picker.node) return false;
        TermView* v = term_view_at(px, py);
        if (!v) return false;
        const Node* node = v->node;
        Event down = pointer_event("mouse_down", px, py, button);
        if (!dispatch(node, down)) return true;
        if (button == 1 || m_focused != node) {
            blur(true);
            blur_code();
            set_focused(node);
        }
        const std::shared_ptr<Terminal> term = terminal_of(document(), *node);
        const bool shift = (m_mods & ModifierShift) != 0;
        const int col = std::clamp(static_cast<int>(std::floor((px - v->x) / v->cell_w)), 0, v->cols - 1);
        const int row = std::clamp(static_cast<int>(std::floor((py - v->y) / v->cell_h)), 0, v->rows - 1);
        if (term->mouse_mode() != TermMouseMode::None && !shift) {
            TermMouse m;
            m.button = button == 1 ? 0 : button == 3 ? 1 : 2;
            m.press = true;
            m.column = col;
            m.row = row;
            m.modifiers = ((m_mods & ModifierAlt) ? TermAlt : 0u) | ((m_mods & ModifierControl) ? TermCtrl : 0u);
            term->send(term->encode_mouse(m));
            m_term_mouse = node;
            m_term_button = m.button;
            m_term_last_col = col;
            m_term_last_row = row;
            poke();
            return true;
        }
        if (button == 1) {
            m_term_select = node;
            m_term_anchor = term_point(*v, *term, px, py, true);
            if (shift && term->has_selection()) {
                term->select(term->selection_from(), m_term_anchor, (m_mods & ModifierAlt) != 0);
            } else {
                term->clear_selection();
            }
            poke();
            return true;
        }
        if (button == 3) term_paste(*node);
        return true;
    }

    void term_mouse_move(float px, float py) {
        if (m_term_select) {
            if (!document().is_attached(*m_term_select)) {
                m_term_select = nullptr;
                return;
            }
            const std::shared_ptr<Terminal> term = terminal_of(document(), *m_term_select);
            if (TermView* v = term_view_of(m_term_select)) {
                if (py < v->y) term->scroll_view(1);
                else if (py >= v->y + v->cell_h * static_cast<float>(v->rows)) term->scroll_view(-1);
                const TermPoint at = term_point(*v, *term, px, py, true);
                if (!(at == m_term_anchor) || term->has_selection()) term->select(m_term_anchor, at, (m_mods & ModifierAlt) != 0);
                poke();
            }
            return;
        }
        const Node* node = m_term_mouse;
        TermView* v = node ? term_view_of(node) : term_view_at(px, py);
        if (!v) return;
        const std::shared_ptr<Terminal> term = terminal_of(document(), *v->node);
        const TermMouseMode mode = term->mouse_mode();
        if (mode != TermMouseMode::ButtonMotion && mode != TermMouseMode::AnyMotion) return;
        if (!node && mode != TermMouseMode::AnyMotion) return;
        const int col = std::clamp(static_cast<int>(std::floor((px - v->x) / v->cell_w)), 0, v->cols - 1);
        const int row = std::clamp(static_cast<int>(std::floor((py - v->y) / v->cell_h)), 0, v->rows - 1);
        if (col == m_term_last_col && row == m_term_last_row) return;
        m_term_last_col = col;
        m_term_last_row = row;
        TermMouse m;
        m.button = node ? m_term_button : 3;
        m.press = true;
        m.motion = true;
        m.column = col;
        m.row = row;
        term->send(term->encode_mouse(m));
    }

    void term_mouse_up(float px, float py, int button) {
        if (m_term_mouse && document().is_attached(*m_term_mouse)) {
            const std::shared_ptr<Terminal> term = terminal_of(document(), *m_term_mouse);
            if (TermView* v = term_view_of(m_term_mouse)) {
                TermMouse m;
                m.button = button == 1 ? 0 : button == 3 ? 1 : 2;
                m.press = false;
                m.column = std::clamp(static_cast<int>(std::floor((px - v->x) / v->cell_w)), 0, v->cols - 1);
                m.row = std::clamp(static_cast<int>(std::floor((py - v->y) / v->cell_h)), 0, v->rows - 1);
                term->send(term->encode_mouse(m));
            }
        }
        m_term_mouse = nullptr;
        if (button == 1 && m_term_select) {
            if (document().is_attached(*m_term_select)) {
                const std::shared_ptr<Terminal> term = terminal_of(document(), *m_term_select);
                if (!term->has_selection()) term->clear_selection();
            }
            m_term_select = nullptr;
        }
    }

    bool term_double_click(float px, float py) {
        TermView* v = term_view_at(px, py);
        if (!v) return false;
        const std::shared_ptr<Terminal> term = terminal_of(document(), *v->node);
        if (term->mouse_mode() != TermMouseMode::None && !(m_mods & ModifierShift)) return true;
        m_term_select = nullptr;
        term->select_word(term_point(*v, *term, px, py, false));
        poke();
        return true;
    }

    bool term_wheel(float px, float py, double dy) {
        TermView* v = term_view_at(px, py);
        if (!v || dy == 0.0) return false;
        const std::shared_ptr<Terminal> term = terminal_of(document(), *v->node);
        const bool up = dy > 0.0;
        const int notches = std::max(1, static_cast<int>(std::lround(std::abs(dy))));
        const bool shift = (m_mods & ModifierShift) != 0;
        const int col = std::clamp(static_cast<int>(std::floor((px - v->x) / v->cell_w)), 0, v->cols - 1);
        const int row = std::clamp(static_cast<int>(std::floor((py - v->y) / v->cell_h)), 0, v->rows - 1);
        if (!shift && (term->mouse_mode() != TermMouseMode::None || (term->alternate_screen() && term->alternate_scroll()))) {
            std::string bytes;
            for (int i = 0; i < notches; ++i) bytes += term->encode_wheel(up, col, row, 0);
            if (!bytes.empty()) term->send(bytes);
            return true;
        }
        term->scroll_view((up ? 3 : -3) * notches);
        poke();
        return true;
    }

    std::size_t doc_order(const Node* node) {
        if (m_doc_order_rev != document().revision()) {
            m_doc_order.clear();
            std::size_t n = 0;
            document().root().walk([&](const Node& x) { m_doc_order[&x] = n++; });
            m_doc_order_rev = document().revision();
        }
        const auto it = m_doc_order.find(node);
        return it == m_doc_order.end() ? 0 : it->second;
    }

    bool text_before(const TextPoint& a, const TextPoint& b) {
        if (a.block == b.block) return a.offset < b.offset;
        return doc_order(a.block) < doc_order(b.block);
    }

    bool has_text_selection() const { return m_sel_anchor.valid() && m_sel_focus.valid() && !(m_sel_anchor == m_sel_focus); }

    void clear_text_selection() {
        const bool had = m_sel_anchor.valid();
        m_sel_anchor = TextPoint{};
        m_sel_focus = TextPoint{};
        m_sel_pending = false;
        m_sel_dragged = false;
        if (had) poke();
    }

    std::pair<std::size_t, std::size_t> run_selection(const TextRun& run) {
        if (!has_text_selection()) return { 0, 0 };
        TextPoint a = m_sel_anchor;
        TextPoint b = m_sel_focus;
        if (text_before(b, a)) std::swap(a, b);
        const TextPoint run_start{ run.block, run.start };
        const TextPoint run_end{ run.block, run.start + run.text.size() };
        if (!text_before(a, run_end) || !text_before(run_start, b)) {
            if (!(a.block == run.block && a.offset <= run.start && b.block == run.block && b.offset >= run_end.offset)) return { 0, 0 };
        }
        std::size_t from = 0;
        std::size_t to = run.text.size();
        if (a.block == run.block && a.offset > run.start) from = std::min(run.text.size(), a.offset - run.start);
        if (b.block == run.block) to = b.offset <= run.start ? 0 : std::min(run.text.size(), b.offset - run.start);
        if (from >= to) return { 0, 0 };
        return { from, to };
    }

    void draw_selection(fizmo::graphics::Renderer& r, const TextRun& run) {
        const auto [from, to] = run_selection(run);
        if (from >= to) return;
        const float x0 = run.x + text_width(r, run.text.substr(0, from), run.style);
        const float x1 = run.x + text_width(r, run.text.substr(0, to), run.style);
        Color color = accent_color(*run.owner);
        if (const auto sel = style_of(*run.owner).color("selection-color")) color = to_color(*sel);
        else color = with_alpha(color, 0.38);
        push_rect(DrawOp::Kind::Fill, x0, run.y, std::max(1.0f, x1 - x0), run.h, color);
    }

    std::size_t run_offset_at(const TextRun& run, float px) {
        fizmo::graphics::Renderer* r = m_renderer;
        if (!r || px <= run.x) return 0;
        if (px >= run.x + run.w) return run.text.size();
        std::vector<std::size_t> ends{ 0 };
        for (std::size_t i = 1; i <= run.text.size(); ++i) {
            if (i == run.text.size() || (static_cast<unsigned char>(run.text[i]) & 0xC0) != 0x80) ends.push_back(i);
        }
        std::size_t best = 0;
        float best_d = 1e9f;
        std::size_t lo = 0;
        std::size_t hi = ends.size() - 1;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (run.x + text_width(*r, run.text.substr(0, ends[mid]), run.style) < px) lo = mid + 1;
            else hi = mid;
        }
        for (std::size_t k = lo > 0 ? lo - 1 : 0; k <= std::min(lo + 1, ends.size() - 1); ++k) {
            const float d = std::abs(run.x + text_width(*r, run.text.substr(0, ends[k]), run.style) - px);
            if (d < best_d) {
                best_d = d;
                best = ends[k];
            }
        }
        return best;
    }

    const TextRun* run_at(float px, float py, bool nearest, const Rect* within = nullptr) {
        const TextRun* best = nullptr;
        float best_score = 1e18f;
        for (const TextRun& run : runs()) {
            if (!run.selectable) continue;
            if (within && (std::abs(run.clip.x - within->x) > 0.5f || std::abs(run.clip.y - within->y) > 0.5f || std::abs(run.clip.w - within->w) > 0.5f || std::abs(run.clip.h - within->h) > 0.5f)) continue;
            if (!nearest) {
                if (!run.clip.contains(px, py)) continue;
                if (py < run.y || py >= run.y + run.h) continue;
                const float dx = px < run.x ? run.x - px : px > run.x + run.w ? px - (run.x + run.w) : 0.0f;
                if (dx < best_score) {
                    best_score = dx;
                    best = &run;
                }
                continue;
            }
            const float dy = py < run.y ? run.y - py : py >= run.y + run.h ? py - (run.y + run.h) : 0.0f;
            const float dx = px < run.x ? run.x - px : px > run.x + run.w ? px - (run.x + run.w) : 0.0f;
            const float score = dy * 1000.0f + dx;
            if (score < best_score) {
                best_score = score;
                best = &run;
            }
        }
        return best;
    }

    TextPoint point_at(float px, float py, bool nearest, const Rect* within = nullptr) {
        const TextRun* run = run_at(px, py, nearest, within);
        if (!run) return TextPoint{};
        return TextPoint{ run->block, run->start + run_offset_at(*run, px) };
    }

    bool selectable_target(const Node* target) {
        for (const Node* n = target; n; n = n->parent()) {
            if (!n->is_element()) continue;
            if (n->tag() == "button" || n->tag() == "terminal" || input_kind_of(*n)) return false;
        }
        return true;
    }

    std::string block_text(const Node* block) {
        std::string out;
        for (const TextRun& run : runs()) {
            if (run.block != block) continue;
            while (out.size() < run.start) out += '\n';
            if (out.size() > run.start) out.resize(run.start);
            out += run.text;
        }
        return out;
    }

    const Rect* anchor_clip() {
        for (const TextRun& run : runs()) {
            if (run.block == m_sel_anchor.block && run.start <= m_sel_anchor.offset && m_sel_anchor.offset <= run.start + run.text.size()) return &run.clip;
        }
        return nullptr;
    }

    void text_drag_to(float px, float py) {
        if (!m_sel_pending) return;
        if (!m_sel_dragged && std::hypot(px - m_sel_press_x, py - m_sel_press_y) < 3.0f) return;
        m_sel_dragged = true;
        const Rect* clip = anchor_clip();
        const Rect keep = clip ? *clip : k_everywhere;
        const TextPoint at = point_at(px, py, true, clip ? &keep : nullptr);
        if (at.valid() && !(at == m_sel_focus)) {
            m_sel_focus = at;
            poke();
        }
    }

    void select_word_at(float px, float py) {
        const TextPoint at = point_at(px, py, false);
        if (!at.valid()) return;
        const std::string text = block_text(at.block);
        auto word = [&](std::size_t i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            return std::isalnum(c) || c == '_' || c >= 0x80;
        };
        std::size_t a = std::min(at.offset, text.size());
        std::size_t b = a;
        if (a < text.size() && !word(a) && a > 0 && word(a - 1)) --a, --b;
        if (a < text.size() && word(a)) {
            while (a > 0 && word(a - 1)) --a;
            while (b < text.size() && word(b)) ++b;
        } else if (a < text.size()) {
            b = a + 1;
        }
        m_sel_anchor = TextPoint{ at.block, a };
        m_sel_focus = TextPoint{ at.block, b };
        poke();
    }

    std::string selection_text() {
        if (!has_text_selection()) return std::string();
        std::string out;
        const TextRun* prev = nullptr;
        for (const TextRun& run : runs()) {
            const auto [from, to] = run_selection(run);
            if (from >= to) continue;
            if (prev) {
                if (prev->block != run.block) out += '\n';
                else {
                    const std::size_t prev_end = prev->start + prev->text.size();
                    if (run.start > prev_end) out += '\n';
                }
            }
            out += run.text.substr(from, to - from);
            prev = &run;
        }
        return out;
    }

    bool copy_text_selection() {
        if (!has_text_selection() || !m_native) return false;
        const std::string text = selection_text();
        if (text.empty()) return false;
        return put_clipboard(m_native, text);
    }

    bool select_contents(const Node& node) {
        if (m_partial_runs) {
            m_pending_select = &node;
            m_force_full = true;
            poke();
            return true;
        }
        const TextRun* first = nullptr;
        const TextRun* last = nullptr;
        for (const TextRun& run : runs()) {
            if (!run.selectable) continue;
            if (run.owner != &node && !run.owner->is_inside(&node) && run.block != &node && !(run.block && run.block->is_inside(&node))) continue;
            if (!first) first = &run;
            last = &run;
        }
        if (!first) return false;
        m_sel_anchor = TextPoint{ first->block, first->start };
        m_sel_focus = TextPoint{ last->block, last->start + last->text.size() };
        poke();
        return true;
    }

    bool completion_enabled(const Node& node) {
        const std::string v = node.attribute_or("autocomplete", "true");
        return v != "false" && v != "off" && !code_read_only(node);
    }

    std::vector<std::string> code_lines(const CodeEditor& ed) const {
        std::vector<std::string> out;
        out.reserve(ed.buffer.line_count());
        for (std::size_t i = 0; i < ed.buffer.line_count(); ++i) out.push_back(ed.buffer.line(i));
        return out;
    }

    bool completion_trigger(const Node& node, const std::string& line, std::size_t column) {
        const std::string triggers = node.attribute_or("complete-triggers", "");
        std::size_t start = 0;
        while (start < triggers.size()) {
            std::size_t end = triggers.find(' ', start);
            if (end == std::string::npos) end = triggers.size();
            const std::string t = triggers.substr(start, end - start);
            if (!t.empty() && column >= t.size() && line.compare(column - t.size(), t.size(), t) == 0) return true;
            start = end + 1;
        }
        return false;
    }

    void close_completion(bool suppress) {
        CodeCompletion& c = m_complete;
        if (suppress) c.waiting = nullptr;
        if (suppress && c.open && c.node) {
            c.suppressed_node = c.node;
            c.suppressed_start = c.start;
        }
        if (c.open) poke();
        c.open = false;
        c.items.clear();
        c.shown.clear();
        c.rows.clear();
    }

    void request_completion(const Node* node, bool manual, const std::string& typed) {
        CodeCompletion& c = m_complete;
        if (!node || c.busy || !document().is_attached(*node) || !completion_enabled(*node)) {
            close_completion(false);
            return;
        }
        CodeEditor& ed = m_code[node];
        CodeBuffer& buf = ed.buffer;
        if (buf.has_extra_cursors() || buf.has_selection()) {
            close_completion(false);
            return;
        }
        const CodePos caret = buf.caret();
        const std::string line = buf.line(caret.line);
        const std::string extra = node->attribute_or("word-chars", "");
        const std::size_t word = completion_word_start(line, caret.column, extra);
        const bool trigger = !typed.empty() && completion_trigger(*node, line, caret.column);
        const std::string prefix = line.substr(word, caret.column - word);
        const bool keep = c.open && c.node == node && c.start.line == caret.line && c.start.column <= caret.column;
        if (!manual && !trigger && !keep) {
            if (prefix.empty() || std::isdigit(static_cast<unsigned char>(prefix[0]))) {
                close_completion(false);
                return;
            }
            if (c.suppressed_node == node && c.suppressed_start == CodePos{ caret.line, word }) return;
        }
        if (keep && caret.column < c.start.column) {
            close_completion(false);
            return;
        }
        c.busy = true;
        Node& mut = mutable_node(*node);
        mut.remove_attribute("completions");
        Event e;
        e.type = "complete";
        e.x = static_cast<double>(caret.line + 1);
        e.y = static_cast<double>(code_display_column(line, caret.column, ed.tab_size) + 1);
        e.key = prefix;
        e.text = typed;
        e.button = manual ? 1 : 0;
        e.bubbles = true;
        const bool allowed = dispatch(node, e);
        c.busy = false;
        if (!document().is_attached(*node)) return;
        std::optional<CompletionList> list;
        if (allowed) list = parse_completions(node->attribute_or("completions", ""));
        CodePos start{ caret.line, trigger && prefix.empty() ? caret.column : word };
        std::vector<CompletionItem> items;
        if (list) {
            if (list->line == caret.line && list->column <= caret.column) start = CodePos{ list->line, list->column };
            items = std::move(list->items);
        } else if (allowed && node->attribute_or("word-completions", "true") != "false") {
            items = word_completions(code_lines(ed), caret.line, caret.column, extra);
            std::set<std::string> have;
            for (const CompletionItem& it : items) have.insert(it.label);
            for (const CodeWord& w : code_language_words(code_language_from_name(node->attribute_or("language", "plain")))) {
                if (!have.insert(w.word).second) continue;
                CompletionItem it;
                it.label = w.word;
                it.kind = w.kind == CodeTokenKind::BuiltinType ? "type" : w.kind == CodeTokenKind::Constant ? "constant" : "keyword";
                items.push_back(std::move(it));
            }
        }
        const std::string typed_part = line.substr(start.column, caret.column - start.column);
        std::vector<CompletionMatch> shown = filter_completions(items, typed_part);
        c.attr_seen = node->attribute_or("completions", "");
        c.waiting = list ? node : nullptr;
        c.waiting_start = start;
        c.waiting_manual = manual;
        if (shown.empty() || (shown.size() == 1 && items[shown[0].index].label == typed_part && !manual)) {
            close_completion(false);
            return;
        }
        std::string keep_label;
        if (c.open && c.node == node && c.active < c.shown.size()) keep_label = c.items[c.shown[c.active].index].label;
        c.node = node;
        c.open = true;
        c.start = start;
        c.items = std::move(items);
        c.shown = std::move(shown);
        c.active = 0;
        if (!keep_label.empty() && !typed_part.empty()) {
            for (std::size_t i = 0; i < c.shown.size() && i < 50; ++i) {
                if (c.items[c.shown[i].index].label == keep_label && c.shown[i].score <= c.shown[0].score) {
                    c.active = i;
                    break;
                }
            }
        }
        c.top = 0;
        c.suppressed_node = nullptr;
        hide_code_hover();
        poke();
    }

    void accept_completion(std::size_t row) {
        CodeCompletion& c = m_complete;
        if (!c.open || !c.node || row >= c.shown.size() || !document().is_attached(*c.node)) {
            close_completion(false);
            return;
        }
        const Node* node = c.node;
        CodeEditor& ed = m_code[node];
        CodeBuffer& buf = ed.buffer;
        const CompletionItem item = c.items[c.shown[row].index];
        const CodePos start = c.start;
        const CodePos caret = buf.caret();
        close_completion(false);
        if (caret.line != start.line || caret.column < start.column) return;
        std::size_t offset = 0;
        const std::string text = completion_insert_text(item, offset);
        buf.select(start, caret);
        buf.insert(text);
        if (offset < text.size()) {
            CodePos at = start;
            for (std::size_t i = 0; i < offset; ++i) {
                if (text[i] == '\n') {
                    ++at.line;
                    at.column = 0;
                } else {
                    ++at.column;
                }
            }
            buf.set_caret(at);
        }
        ed.column_mode = false;
        code_changed(node, text);
        Event done;
        done.type = "completion";
        done.key = item.label;
        done.text = text;
        done.bubbles = true;
        dispatch(node, done);
    }

    void after_code_typing(const Node* node, const std::string& typed) {
        if (!node || typed.empty() || !completion_enabled(*node)) return;
        CodeEditor& ed = m_code[node];
        const CodePos caret = ed.buffer.caret();
        const std::string line = ed.buffer.line(caret.line);
        const std::string extra = node->attribute_or("word-chars", "");
        const char last = typed.back();
        if (is_completion_word_char(last, extra) || completion_trigger(*node, line, caret.column)) request_completion(node, false, typed);
        else close_completion(false);
    }

    void step_completion(long delta) {
        CodeCompletion& c = m_complete;
        if (c.shown.empty()) return;
        const long n = static_cast<long>(c.shown.size());
        long next = static_cast<long>(c.active) + delta;
        if (delta == 1 || delta == -1) next = (next % n + n) % n;
        else next = std::clamp(next, 0L, n - 1);
        c.active = static_cast<std::size_t>(next);
        if (c.active < c.top) c.top = c.active;
        if (c.visible > 0 && c.active >= c.top + c.visible) c.top = c.active + 1 - c.visible;
        poke();
    }

    bool completion_key(fizmo::input::Key key, fizmo::input::Modifiers mods) {
        using fizmo::input::Key;
        using fizmo::input::Modifiers;
        const bool ctrl = fizmo::input::has(mods, Modifiers::Control) || fizmo::input::has(mods, Modifiers::Meta);
        const bool shift = fizmo::input::has(mods, Modifiers::Shift);
        const bool alt = fizmo::input::has(mods, Modifiers::Alt);
        CodeCompletion& c = m_complete;
        if (ctrl && !alt && key == Key::Space) {
            if (m_code_focus && completion_enabled(*m_code_focus)) request_completion(m_code_focus, true, std::string());
            return true;
        }
        if (!c.open || c.node != m_code_focus) return false;
        if (ctrl || alt) {
            close_completion(false);
            return false;
        }
        switch (key) {
            case Key::Up: step_completion(-1); return true;
            case Key::Down: step_completion(1); return true;
            case Key::PageUp: step_completion(-static_cast<long>(std::max<std::size_t>(1, c.visible - 1))); return true;
            case Key::PageDown: step_completion(static_cast<long>(std::max<std::size_t>(1, c.visible - 1))); return true;
            case Key::Tab:
                if (shift) return false;
                accept_completion(c.active);
                return true;
            case Key::Enter:
            case Key::NumpadEnter:
                if (m_code_focus->attribute_or("accept-on-enter", "false") == "true") {
                    accept_completion(c.active);
                    return true;
                }
                close_completion(false);
                return false;
            case Key::Escape:
                close_completion(true);
                return true;
            case Key::Backspace:
            case Key::Delete:
                return false;
            case Key::LeftShift:
            case Key::RightShift:
                return false;
            default:
                close_completion(false);
                return false;
        }
    }

    void after_code_key(fizmo::input::Key key) {
        using fizmo::input::Key;
        CodeCompletion& c = m_complete;
        if (!c.open || c.node != m_code_focus) return;
        if (key == Key::Backspace || key == Key::Delete) {
            const CodePos caret = m_code[c.node].buffer.caret();
            if (caret.line != c.start.line || caret.column < c.start.column || (caret.column == c.start.column && c.start.column == completion_word_start(m_code[c.node].buffer.line(caret.line), caret.column, c.node->attribute_or("word-chars", "")) && !completion_trigger(*c.node, m_code[c.node].buffer.line(caret.line), caret.column))) {
                close_completion(false);
                return;
            }
            request_completion(c.node, false, std::string());
        }
    }

    bool completion_click(float px, float py) {
        CodeCompletion& c = m_complete;
        if (!c.open) return false;
        if (!c.panel.contains(px, py)) {
            close_completion(false);
            return false;
        }
        for (const auto& [rect, index] : c.rows) {
            if (rect.contains(px, py)) {
                accept_completion(index);
                return true;
            }
        }
        return true;
    }

    bool completion_wheel(double dy) {
        CodeCompletion& c = m_complete;
        if (!c.open || !c.panel.contains(m_mouse_x, m_mouse_y)) return false;
        const long steps = dy > 0 ? -3 : 3;
        const long top = std::clamp(static_cast<long>(c.top) + steps, 0L, static_cast<long>(c.shown.size() > c.visible ? c.shown.size() - c.visible : 0));
        c.top = static_cast<std::size_t>(top);
        poke();
        return true;
    }

    struct KindLook {
        const char* badge;
        Color color;
    };

    static KindLook kind_look(const std::string& kind) {
        if (kind == "keyword") return { "K", Color(198, 120, 221, 255) };
        if (kind == "function" || kind == "method" || kind == "constructor" || kind == "macro-function") return { kind == "method" ? "M" : "F", Color(229, 192, 123, 255) };
        if (kind == "variable" || kind == "parameter" || kind == "local") return { "V", Color(97, 175, 239, 255) };
        if (kind == "field" || kind == "property" || kind == "member") return { "P", Color(86, 182, 194, 255) };
        if (kind == "type" || kind == "class" || kind == "struct" || kind == "enum" || kind == "union" || kind == "interface" || kind == "typedef") return { "T", Color(230, 150, 90, 255) };
        if (kind == "namespace" || kind == "module" || kind == "import") return { "N", Color(152, 195, 121, 255) };
        if (kind == "constant" || kind == "enum member" || kind == "macro" || kind == "value") return { "C", Color(209, 154, 102, 255) };
        if (kind == "tag" || kind == "element") return { "<", Color(224, 108, 117, 255) };
        if (kind == "attribute") return { "@", Color(209, 154, 102, 255) };
        if (kind == "snippet") return { "S", Color(171, 178, 191, 255) };
        if (kind == "file" || kind == "folder") return { "D", Color(171, 178, 191, 255) };
        if (kind == "character" || kind == "symbol") return { "\xE2\x84\xA6", Color(198, 160, 246, 255) };
        return { "W", Color(140, 146, 160, 255) };
    }

    static std::string kind_name(const std::string& kind) {
        if (kind.empty() || kind == "word") return "word";
        if (kind == "macro-function") return "macro";
        if (kind == "local") return "variable";
        if (kind == "member") return "field";
        return kind;
    }

    void completion_from_attribute() {
        CodeCompletion& c = m_complete;
        const Node* node = c.open ? c.node : c.waiting;
        if (!node || node != m_code_focus || !document().is_attached(*node)) {
            c.waiting = nullptr;
            return;
        }
        const std::string attr = node->attribute_or("completions", "");
        if (attr == c.attr_seen) return;
        c.attr_seen = attr;
        const auto found = m_code.find(node);
        if (found == m_code.end()) return;
        const CodeBuffer& buf = found->second.buffer;
        const CodePos caret = buf.caret();
        const CodePos start = c.open ? c.start : c.waiting_start;
        if (caret.line != start.line || caret.column < start.column) {
            c.waiting = nullptr;
            return;
        }
        const std::string line = buf.line(caret.line);
        const std::string extra = node->attribute_or("word-chars", "");
        for (std::size_t i = start.column; i < caret.column; ++i) {
            if (!is_completion_word_char(line[i], extra)) {
                c.waiting = nullptr;
                return;
            }
        }
        std::optional<CompletionList> list = parse_completions(attr);
        if (!list) return;
        CodePos at = start;
        if (list->line == caret.line && list->column <= caret.column) at = CodePos{ list->line, list->column };
        const std::string typed = line.substr(at.column, caret.column - at.column);
        std::vector<CompletionMatch> shown = filter_completions(list->items, typed);
        if (shown.empty() || (shown.size() == 1 && list->items[shown[0].index].label == typed && !c.waiting_manual)) return;
        std::string keep_label;
        if (c.open && c.active < c.shown.size()) keep_label = c.items[c.shown[c.active].index].label;
        c.node = node;
        c.start = at;
        c.items = std::move(list->items);
        c.shown = std::move(shown);
        c.active = 0;
        for (std::size_t i = 0; !keep_label.empty() && i < c.shown.size() && i < 50; ++i) {
            if (c.items[c.shown[i].index].label == keep_label) {
                c.active = i;
                break;
            }
        }
        c.open = true;
        hide_code_hover();
    }

    void layout_completion(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        CodeCompletion& c = m_complete;
        c.rows.clear();
        completion_from_attribute();
        if (!c.open) return;
        if (!c.node || c.node != m_code_focus || !document().is_attached(*c.node)) {
            close_completion(false);
            return;
        }
        const auto found = m_code.find(c.node);
        if (found == m_code.end()) return;
        CodeEditor& ed = found->second;
        if (c.start.line >= ed.buffer.line_count()) {
            close_completion(false);
            return;
        }
        const bark::ComputedStyle& style = style_of(*c.node);
        const Color fg = raw_text_color(*c.node);
        const Color accent = accent_color(*c.node);
        const Color editor_bg = to_color(style.color("background").value_or(bark::Rgba{ 30, 30, 46, 255 }));
        const std::optional<bark::Rgba> hover_bg = style.color("code-hover");
        const Color panel_bg = hover_bg ? to_color(*hover_bg) : mix(editor_bg, fg, 0.06f);
        fizmo::text::TextStyle mono = text_style(*c.node);
        if (!style.get("font-family")) mono.set_font(fizmo::text::to_font(fizmo::text::MonospaceFont::CascadiaMono));
        mono.set_size(std::max(10.0, mono.size() * 0.95));
        mono.set_bold(false);
        mono.set_italic(false);
        mono.set_underline(false);
        mono.set_strikethrough(false);
        mono.set_fg_color(fg);
        fizmo::text::TextStyle bold = mono;
        bold.set_bold(true);
        bold.set_fg_color(accent);
        fizmo::text::TextStyle muted = mono;
        muted.set_fg_color(with_alpha(fg, 0.55));
        fizmo::text::TextStyle prose(std::max(11.0, mono.size() * 0.92), with_alpha(fg, 0.85));
        fizmo::text::TextStyle preview_style = mono;
        preview_style.set_size(mono.size() * 1.3);
        preview_style.set_fg_color(fg);
        fizmo::text::TextStyle badge_style = mono;
        badge_style.set_bold(true);
        badge_style.set_size(std::max(9.0, mono.size() * 0.8));
        const float lh = line_height(r, mono);
        c.row_h = std::round(lh + 6.0f);
        const std::size_t max_rows = static_cast<std::size_t>(std::clamp<std::int64_t>(parse_integer(c.node->attribute_or("complete-rows", "")).value_or(10), 3, 30));
        c.visible = std::min(max_rows, c.shown.size());
        if (c.active >= c.shown.size()) c.active = 0;
        if (c.top + c.visible > c.shown.size()) c.top = c.shown.size() - c.visible;
        if (c.active < c.top) c.top = c.active;
        if (c.active >= c.top + c.visible) c.top = c.active + 1 - c.visible;
        const bool show_icons = c.node->attribute_or("complete-icons", "true") != "false";
        const bool show_kinds = c.node->attribute_or("complete-kinds", "false") != "false";
        float widest_label = 0.0f;
        float widest_detail = 0.0f;
        float widest_kind = 0.0f;
        float widest_preview = 0.0f;
        for (std::size_t i = 0; i < c.shown.size() && i < 200; ++i) {
            const CompletionItem& it = c.items[c.shown[i].index];
            widest_label = std::max(widest_label, text_width(r, it.label, mono));
            if (!it.preview.empty()) widest_preview = std::max(widest_preview, std::min(120.0f, text_width(r, it.preview, preview_style)));
            const std::string kind = show_kinds ? kind_name(it.kind) : std::string();
            if (show_kinds) widest_kind = std::max(widest_kind, text_width(r, kind, muted));
            if (!it.detail.empty() && it.detail != kind) widest_detail = std::max(widest_detail, std::min(260.0f, text_width(r, it.detail, muted)));
        }
        const float kind_room = widest_kind > 0.0f ? widest_kind + 18.0f : 0.0f;
        const float badge = show_icons ? c.row_h - 6.0f : 0.0f;
        const float max_w = static_cast<float>(std::max<std::int64_t>(240, parse_integer(c.node->attribute_or("complete-width", "")).value_or(760)));
        const float limit_w = std::max(220.0f, std::min(max_w, view_w - 8.0f));
        const float preview_room = widest_preview > 0.0f ? widest_preview + 16.0f : 0.0f;
        const float fixed_w = 8.0f + badge + 8.0f + kind_room + preview_room + 14.0f;
        float detail_col = std::min(widest_detail, 320.0f);
        float label_col = widest_label + 18.0f;
        if (fixed_w + label_col + (detail_col > 0.0f ? detail_col + 16.0f : 0.0f) > limit_w && detail_col > 0.0f) {
            const float room = limit_w - fixed_w - label_col - 16.0f;
            detail_col = std::max(std::min(detail_col, std::max(120.0f, limit_w * 0.34f)), room);
        }
        if (fixed_w + label_col + (detail_col > 0.0f ? detail_col + 16.0f : 0.0f) > limit_w) label_col = std::max(60.0f, limit_w - fixed_w - (detail_col > 0.0f ? detail_col + 16.0f : 0.0f));
        const float detail_room_all = detail_col > 0.0f ? detail_col + 16.0f : 0.0f;
        const float panel_w = std::min(std::max(220.0f, fixed_w + label_col + detail_room_all), limit_w);
        const CompletionItem& active = c.items[c.shown[c.active].index];
        std::vector<std::string> doc_lines;
        if (!active.documentation.empty()) {
            std::string para;
            std::istringstream in(active.documentation);
            std::string raw;
            while (std::getline(in, raw) && doc_lines.size() < 6) {
                std::string line;
                std::istringstream words(raw);
                std::string w;
                while (words >> w) {
                    const std::string trial = line.empty() ? w : line + " " + w;
                    if (!line.empty() && text_width(r, trial, prose) > panel_w - 20.0f) {
                        doc_lines.push_back(line);
                        line = w;
                        if (doc_lines.size() >= 6) break;
                    } else {
                        line = trial;
                    }
                }
                if (!line.empty() && doc_lines.size() < 6) doc_lines.push_back(line);
            }
        }
        const float prose_h = line_height(r, prose) + 2.0f;
        const float doc_h = doc_lines.empty() ? 0.0f : static_cast<float>(doc_lines.size()) * prose_h + 12.0f;
        const float panel_h = static_cast<float>(c.visible) * c.row_h + 6.0f + doc_h;
        const std::string& line = ed.buffer.line(c.start.line);
        const float ax = ed.text_x + static_cast<float>(code_display_column(line, std::min(c.start.column, line.size()), ed.tab_size)) * ed.char_w - ed.scroll_x - badge - 14.0f;
        const float line_top = ed.text_y + static_cast<float>(c.start.line) * ed.line_h - ed.scroll_y;
        float px = std::clamp(ax, 4.0f, std::max(4.0f, view_w - panel_w - 4.0f));
        float py = line_top + ed.line_h + 2.0f;
        if (py + panel_h > view_h - 4.0f && line_top - 2.0f - panel_h >= 4.0f) py = line_top - 2.0f - panel_h;
        py = std::clamp(py, 4.0f, std::max(4.0f, view_h - panel_h - 4.0f));
        c.panel = Rect{ px, py, panel_w, panel_h };
        push_rect(DrawOp::Kind::Fill, px + 2, py + 3, panel_w, panel_h, Color(0, 0, 0, 80), 1, 6);
        push_rect(DrawOp::Kind::Fill, px, py, panel_w, panel_h, panel_bg, 1, 6);
        push_rect(DrawOp::Kind::Stroke, px, py, panel_w, panel_h, with_alpha(fg, 0.22), 1, 6);
        push_clip(Rect{ px + 1, py + 1, panel_w - 2, panel_h - 2 });
        for (std::size_t vi = 0; vi < c.visible; ++vi) {
            const std::size_t i = c.top + vi;
            if (i >= c.shown.size()) break;
            const CompletionMatch& m = c.shown[i];
            const CompletionItem& it = c.items[m.index];
            const Rect row{ px + 3, py + 3 + static_cast<float>(vi) * c.row_h, panel_w - 6, c.row_h };
            const bool hovered = row.contains(m_mouse_x, m_mouse_y);
            if (i == c.active) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(accent, 0.28), 1, 4);
            else if (hovered) push_rect(DrawOp::Kind::Fill, row.x, row.y, row.w, row.h, with_alpha(fg, 0.07), 1, 4);
            const KindLook look = kind_look(it.kind);
            const float bx = row.x + 5.0f;
            const float by = row.y + 3.0f;
            if (show_icons) {
                push_rect(DrawOp::Kind::Fill, bx, by, badge, badge, with_alpha(look.color, 0.22), 1, 3);
                fizmo::text::TextStyle bs = badge_style;
                bs.set_fg_color(look.color);
                const float bw = text_width(r, look.badge, bs);
                push_text(bx + (badge - bw) / 2.0f, by + (badge - line_height(r, bs)) / 2.0f, bw + 4.0f, line_height(r, bs), look.badge, bs);
            }
            float tx = show_icons ? bx + badge + 8.0f : bx + 3.0f;
            const float ty = row.y + (c.row_h - lh) / 2.0f;
            const std::string kind_text = show_kinds ? kind_name(it.kind) : std::string();
            const bool show_detail = !it.detail.empty() && it.detail != kind_text;
            const float label_room = std::max(20.0f, row.x + row.w - 8.0f - detail_room_all - kind_room - preview_room - tx - 6.0f);
            if (!it.preview.empty()) {
                const float preview_x = tx + label_room + 6.0f;
                const float ph = line_height(r, preview_style);
                push_text(preview_x, row.y + (c.row_h - ph) / 2.0f, widest_preview + 4.0f, ph, fit_text(r, it.preview, preview_style, widest_preview), preview_style);
            }
            push_clip(Rect{ tx, row.y, label_room, row.h });
            std::size_t k = 0;
            std::size_t mi = 0;
            const std::string& label = it.label;
            const std::string fitted = fit_text(r, label, mono, label_room - 4.0f);
            const bool cut = fitted.size() < label.size() || fitted != label;
            const std::size_t visible = cut && fitted.size() >= 3 ? fitted.size() - 3 : label.size();
            while (k < visible) {
                const bool matched = mi < m.positions.size() && m.positions[mi] == k;
                std::size_t e = k;
                std::size_t mj = mi;
                while (e < label.size() && ((mj < m.positions.size() && m.positions[mj] == e) == matched)) {
                    if (matched) ++mj;
                    ++e;
                }
                if (e > visible) e = visible;
                const std::string piece = label.substr(k, e - k);
                const fizmo::text::TextStyle& st = matched ? bold : mono;
                const float pw = text_width(r, piece, st);
                push_text(tx, ty, pw + 4.0f, lh, piece, st);
                tx += pw;
                k = e;
                mi = mj;
            }
            if (cut) push_text(tx, ty, text_width(r, "\xE2\x80\xA6", mono) + 4.0f, lh, "\xE2\x80\xA6", mono);
            pop_clip();
            if (show_kinds) {
                fizmo::text::TextStyle ks = muted;
                ks.set_fg_color(with_alpha(show_icons ? look.color : fg, 0.75));
                const float kx = row.x + row.w - 8.0f - detail_room_all - widest_kind;
                push_text(kx, ty, widest_kind + 4.0f, lh, kind_text, ks);
            }
            if (show_detail) {
                const std::string shown = fit_text(r, it.detail, muted, detail_col);
                const float dw = text_width(r, shown, muted);
                push_text(row.x + row.w - 8.0f - dw, ty, dw + 4.0f, lh, shown, muted);
            }
            c.rows.emplace_back(row, i);
        }
        if (c.shown.size() > c.visible && c.visible > 0) {
            const float track = static_cast<float>(c.visible) * c.row_h;
            const float thumb = std::max(16.0f, track * static_cast<float>(c.visible) / static_cast<float>(c.shown.size()));
            const float at = (track - thumb) * static_cast<float>(c.top) / static_cast<float>(c.shown.size() - c.visible);
            push_rect(DrawOp::Kind::Fill, px + panel_w - 5.0f, py + 3.0f + at, 3.0f, thumb, with_alpha(fg, 0.3), 1, 2);
        }
        if (!doc_lines.empty()) {
            const float dy = py + 3.0f + static_cast<float>(c.visible) * c.row_h + 3.0f;
            push_rect(DrawOp::Kind::Fill, px + 1.0f, dy, panel_w - 2.0f, 1.0f, with_alpha(fg, 0.15));
            float y = dy + 5.0f;
            for (const std::string& dl : doc_lines) {
                push_text(px + 10.0f, y, panel_w - 20.0f, prose_h, dl, prose);
                y += prose_h;
            }
        }
        pop_clip();
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
        const bool hot = m_dragging == &element || (element.has_state(StateHover) && !element.has_attribute("disabled"));
        const float base_thumb = fs * 0.55f;
        const float thumb = hot ? base_thumb * 1.3f : base_thumb;
        const float w = box_width(style, width);
        const float track_x = x + base_thumb;
        const float track_w = std::max(1.0f, w - 2 * base_thumb);
        const InputRange range = input_range(element);
        const double value = input_float(element).value_or(range.min);
        const double t = range.max > range.min ? (value - range.min) / (range.max - range.min) : 0.0;
        const float vx = track_x + static_cast<float>(t) * track_w;
        const float cy = y + row / 2.0f;
        const bool disabled = element.has_attribute("disabled");
        const double fade = (disabled ? 0.45 : 1.0) * opacity_of(element);
        const Color accent = with_alpha(accent_color(element), fade);

        const float track_h = hot ? 6.0f : 4.0f;
        push_rect(DrawOp::Kind::Fill, track_x, cy - track_h / 2.0f, track_w, track_h, with_alpha(raw_text_color(element), 0.2 * fade), 1, 2);
        push_rect(DrawOp::Kind::Fill, track_x, cy - track_h / 2.0f, vx - track_x, track_h, accent, 1, 2);
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

    static std::uint64_t line_hash(const std::string& line) noexcept {
        std::uint64_t h = 14695981039346656037ull;
        for (const unsigned char c : line) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    void rehighlight(CodeEditor& ed, CodeLanguage lang) {
        const std::size_t count = ed.buffer.line_count();
        std::vector<std::uint64_t> hashes(count);
        for (std::size_t i = 0; i < count; ++i) hashes[i] = line_hash(ed.buffer.line(i));
        const std::size_t old_n = ed.states.size();
        const bool reuse = old_n > 0 && ed.line_hashes.size() == old_n && ed.states_language == static_cast<int>(lang);
        std::vector<CodeLineState> states(count);
        std::size_t start = 0;
        std::size_t suffix = 0;
        if (reuse) {
            std::size_t prefix = 0;
            const std::size_t both = std::min(count, old_n);
            while (prefix < both && hashes[prefix] == ed.line_hashes[prefix]) ++prefix;
            while (suffix < both - prefix && hashes[count - 1 - suffix] == ed.line_hashes[old_n - 1 - suffix]) ++suffix;
            start = prefix > 0 ? prefix - 1 : 0;
            for (std::size_t i = 0; i < start; ++i) states[i] = ed.states[i];
        }
        CodeLineState st = reuse && start < old_n ? ed.states[start] : CodeLineState{};
        for (std::size_t i = start; i < count; ++i) {
            if (reuse && suffix > 0 && i >= count - suffix && i > start) {
                const std::size_t j = i + old_n - count;
                if (st == ed.states[j]) {
                    for (std::size_t k = i; k < count; ++k) states[k] = ed.states[k + old_n - count];
                    break;
                }
            }
            states[i] = st;
            highlight_code_line(lang, ed.buffer.line(i), st);
        }
        ed.states = std::move(states);
        ed.line_hashes = std::move(hashes);
        ed.states_language = static_cast<int>(lang);
    }

    void layout_code(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, float forced_h) {
        const bark::ComputedStyle& style = style_of(element);
        CodeEditor& ed = m_code[&element];
        m_code_laid.push_back(&element);
        const bool node_changed = ed.seen_stamp != element.tree_stamp();
        if (node_changed) {
            const std::string value = input_string(element).value_or("");
            if (value != ed.synced) {
                const CodePos keep = ed.buffer.caret();
                ed.buffer.set_text(value);
                ed.buffer.set_caret(keep);
                ed.synced = value;
            }
            ed.tab_size = static_cast<std::size_t>(std::max<std::int64_t>(1, parse_integer(element.attribute_or("tab-size", "4")).value_or(4)));
        }
        const std::string caret_attr = node_changed ? element.attribute_or("caret", "") : std::string();
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
        if (node_changed) read_find_attributes(element, ed);
        refresh_find(ed);
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
            rehighlight(ed, lang);
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
        if (node_changed) {
            const Attribute* spans_attr = element.find_attribute("semantic");
            const std::string_view spans_text = spans_attr ? std::string_view(spans_attr->value) : std::string_view();
            if (spans_text != ed.overlay_spans) {
                ed.overlay_spans = std::string(spans_text);
                ed.overlay.set_spans(ed.overlay_spans);
            }
            const Attribute* problems_attr = element.find_attribute("problems");
            const std::string_view problems_text = problems_attr ? std::string_view(problems_attr->value) : std::string_view();
            if (problems_text != ed.overlay_problems) {
                ed.overlay_problems = std::string(problems_text);
                ed.overlay.set_problems(ed.overlay_problems);
            }
            ed.seen_stamp = element.tree_stamp();
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
        const bool swatches = (lang == CodeLanguage::Bark || lang == CodeLanguage::Grove || lang == CodeLanguage::Css || lang == CodeLanguage::Html) && element.attribute_or("color-swatches", "true") != "false";
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
        const Color find_color = to_color(style.color("code-find").value_or(bark::Rgba{ 255, 165, 60, 85 }));
        const Color find_current_color = to_color(style.color("code-find-current").value_or(bark::Rgba{ 255, 165, 60, 170 }));
        fizmo::text::TextStyle number_style = ts;
        number_style.set_bold(false);
        number_style.set_italic(false);
        number_style.set_underline(false);
        number_style.set_strikethrough(false);

        push_clip(Rect{ inner_x, inner_y, inner_w, inner_h });
        for (std::size_t li = first; li < last; ++li) {
            const float ly = inner_y + static_cast<float>(li) * lh - ed.scroll_y;
            const std::string& line = ed.buffer.line(li);
            bool caret_line = false;
            for (const CodeCursor& cur : cursors) caret_line = caret_line || (cur.caret.line == li && !cur.has_selection());
            if (focused && caret_line) push_rect(DrawOp::Kind::Fill, inner_x + gutter, ly, inner_w - gutter, lh, with_alpha(Color(255, 255, 255, 255), 0.04));
            if (!ed.matches.empty()) {
                const auto from = std::lower_bound(ed.matches.begin(), ed.matches.end(), CodePos{ li, 0 }, [](const CodeMatch& m, CodePos p) { return m.begin < p; });
                for (auto it = from; it != ed.matches.end() && it->begin.line == li; ++it) {
                    const std::size_t a = code_display_column(line, it->begin.column, ed.tab_size);
                    const std::size_t b = code_display_column(line, it->end.column, ed.tab_size);
                    const float mx = text_x + static_cast<float>(a) * cw - ed.scroll_x;
                    const float mw = std::max(2.0f, static_cast<float>(b - std::min(a, b)) * cw);
                    const bool current = static_cast<std::size_t>(it - ed.matches.begin()) == ed.find_current;
                    push_rect(DrawOp::Kind::Fill, mx, ly + 1, mw, lh - 2, with_alpha(current ? find_current_color : find_color, fade), 1, 2);
                    if (current) push_rect(DrawOp::Kind::Stroke, mx, ly + 1, mw, lh - 2, with_alpha(find_current_color, fade), 1, 2);
                }
            }
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
                    look.strikethrough = look.strikethrough || error_line.strikethrough;
                }
                const Color c = look.color.value_or(fg);
                if (look.bold) tok.set_bold(true);
                if (look.italic) tok.set_italic(true);
                if (look.underline) tok.set_underline(true);
                if (look.strikethrough) tok.set_strikethrough(true);
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
        Scroller code_bar{ &element, current_clip().intersect(Rect{ x, y, w, h }), max_y, true };
        if (static_cast<float>(count) * lh > inner_h) {
            const float track = inner_h;
            const float thumb = std::max(24.0f, track * inner_h / (content_h));
            const float ty = inner_y + (track - thumb) * (ed.scroll_y / max_y);
            const bool held = m_bar_drag.node == &element || (m_bar_hot.node == &element && !m_bar_hot.horizontal);
            push_rect(DrawOp::Kind::Fill, x + w - (held ? 8 : 7), ty, held ? 6 : 4, thumb, with_alpha(fg, held ? 0.45 : 0.25), 1, 2);
            code_bar.bar_x = x + w - 13;
            code_bar.track_y = inner_y;
            code_bar.track_h = track;
            code_bar.thumb_h = thumb;
        }
        pop_clip();
        Color border = to_color(style.color("border-color").value_or(bark::Rgba{ 255, 255, 255, 40 }));
        if (focused) border = accent;
        if (style.get("border-color") || focused) push_rect(DrawOp::Kind::Stroke, x, y, w, h, with_alpha(border, fade), focused ? 2u : static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);

        if (!disabled) add_hit(Hit{ &element, InputKind::Code, x, y, w, h, 0, 0 });
        add_box(ElementBox{ &element, x, y, w, h });
        m_scrollers.push_back(code_bar);
        y += h;
    }

    struct CanvasTexture {
        std::uint64_t version = 0;
        std::shared_ptr<fizmo::images::BitmapImage> image;
        std::shared_ptr<fizmo::graphics::Texture> texture;
    };

    const fizmo::graphics::Texture* canvas_texture(const Node& element) {
        const std::shared_ptr<CanvasPixels> c = canvas_of(document(), element);
        if (c->width == 0 || c->height == 0 || c->rgba.size() < static_cast<std::size_t>(c->width) * c->height * 4u) return nullptr;
        CanvasTexture& t = m_canvas_textures[&element];
        if (t.version != c->version || !t.texture) {
            if (!t.image || t.image->width() != c->width || t.image->height() != c->height) {
                t.image = std::make_shared<fizmo::images::BitmapImage>(c->width, c->height);
                t.texture = std::make_shared<fizmo::graphics::Texture>(std::shared_ptr<const fizmo::images::BitmapImage>(t.image), fizmo::graphics::SampleFilter::Bilinear);
            }
            std::vector<fizmo::colors::Color>& px = t.image->mutable_pixels();
            const std::uint8_t* src = c->rgba.data();
            const std::size_t n = static_cast<std::size_t>(c->width) * c->height;
            for (std::size_t i = 0; i < n && i < px.size(); ++i, src += 4) px[i] = fizmo::colors::Color(src[0], src[1], src[2], src[3]);
            t.version = c->version;
        }
        return t.texture && t.texture->valid() ? t.texture.get() : nullptr;
    }

    void layout_canvas(const Node& element, float x, float& y, float width, float height) {
        float h = height;
        if (h < 0.0f) {
            const float zoom = static_cast<float>(m_styles.viewport().scale());
            if (const auto a = parse_float(element.attribute_or("height", ""))) h = static_cast<float>(*a) * zoom;
            else h = std::max(60.0f, std::round(width * 0.6f));
        }
        h = std::max(0.0f, h);
        if (const fizmo::graphics::Texture* tex = canvas_texture(element)) {
            DrawOp& op = push(DrawOp::Kind::Image, x, y, width, h, Color());
            op.texture = tex;
            op.alpha = static_cast<float>(opacity_of(element));
        }
        y += h;
    }

    bool in_this_window(const Node& node) const {
        for (const Node* n = node.parent(); n; n = n->parent()) {
            if (n == &m_window) return true;
            if (n->is_element() && n->tag() == "window") return false;
        }
        return false;
    }

    void publish_canvases() {
        auto& all = document().host().canvases;
        if (all.empty()) return;
        std::set<const Node*> seen;
        for (const ElementBox& b : m_boxes) {
            if (!b.node || b.node->tag() != "canvas") continue;
            auto it = all.find(b.node);
            if (it == all.end()) it = all.emplace(b.node, std::make_shared<CanvasPixels>()).first;
            if (!seen.insert(b.node).second) continue;
            const bark::ComputedStyle& cs = style_of(*b.node);
            const Box pad = box_of(cs, "padding");
            CanvasRect& rect = it->second->rect;
            rect.x = b.x + pad.left;
            rect.y = b.y + pad.top;
            rect.width = std::max(0.0f, b.w - pad.left - pad.right);
            rect.height = std::max(0.0f, b.h - pad.top - pad.bottom);
            rect.shown = true;
        }
        for (auto& [node, c] : all) {
            if (!seen.count(node)) {
                if (c->rect.shown && document().is_attached(*node) && in_this_window(*node)) c->rect.shown = false;
                continue;
            }
            const unsigned w = static_cast<unsigned>(std::max(0.0, std::round(c->rect.width)));
            const unsigned h = static_cast<unsigned>(std::max(0.0, std::round(c->rect.height)));
            if (w == c->reported_width && h == c->reported_height) continue;
            c->reported_width = w;
            c->reported_height = h;
            const Node* target = node;
            document().events().set_timeout(0.0, [this, target, w, h] {
                if (!document().is_attached(*target)) return;
                Event e;
                e.type = "canvas_resize";
                e.bubbles = false;
                e.x = w;
                e.y = h;
                dispatch(target, e);
            });
        }
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
        const float zoom = static_cast<float>(m_styles.viewport().scale());
        const float nw = tex ? static_cast<float>(tex->width()) * zoom : 0.0f;
        const float nh = tex ? static_cast<float>(tex->height()) * zoom : 0.0f;
        std::optional<float> sw = resolve_length(style, "width", available);
        std::optional<float> sh = resolve_length(style, "height", -1.0f);
        if (!sw) {
            if (const auto a = parse_float(img.attribute_or("width", ""))) sw = static_cast<float>(*a) * zoom;
        }
        if (!sh) {
            if (const auto a = parse_float(img.attribute_or("height", ""))) sh = static_cast<float>(*a) * zoom;
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
        if (m_wide == 0 && w > available && available > 0.0f && w > 0.0f) {
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

    struct LayoutEntry {
        std::uint64_t stamp = 0;
        std::uint64_t style = 0;
        std::uint64_t generation = 0;
        std::uint64_t used = 0;
        float width = 0.0f;
        float avail = 0.0f;
        float forced = 0.0f;
        bool sized = false;
        bool wide = false;
        bool dynamic = false;
        bool measured = false;
        bool captured = false;
        unsigned retries = 0;
        unsigned streak = 0;
        std::uint64_t changed_frame = 0;
        float advance = 0.0f;
        Rect bounds;
        std::vector<DrawOp> ops;
        std::vector<ElementBox> boxes;
        std::vector<TextRun> runs;
    };

    std::uint64_t layout_generation() const {
        return m_styles.revision();
    }

    bool captures_cleanly(const Mark& from) const {
        if (m_hits.size() != from.hits || m_scrollers.size() != from.scrollers || m_swatches.size() != from.swatches || m_code_laid.size() != from.codes || m_term_views.size() != from.terms) return false;
        for (std::size_t i = from.ops; i < m_ops.size(); ++i) {
            const DrawOp::Kind k = m_ops[i].kind;
            if (k == DrawOp::Kind::ClipPush || k == DrawOp::Kind::ClipPop || k == DrawOp::Kind::Math || k == DrawOp::Kind::Image) return false;
        }
        return true;
    }

    Rect slice_bounds(const Mark& from, float x, float y0, float advance) const {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = advance;
        for (std::size_t i = from.ops; i < m_ops.size(); ++i) {
            const DrawOp& op = m_ops[i];
            if (op.kind == DrawOp::Kind::ClipPop) continue;
            const float pad = op.kind == DrawOp::Kind::Circle || op.kind == DrawOp::Kind::CircleStroke ? op.radius : static_cast<float>(op.stroke) + 2.0f;
            left = std::min(left, op.x - x - pad);
            top = std::min(top, op.y - y0 - pad);
            right = std::max(right, op.x - x + op.w + pad);
            bottom = std::max(bottom, op.y - y0 + op.h + pad);
        }
        for (std::size_t i = from.boxes; i < m_boxes.size(); ++i) {
            const ElementBox& b = m_boxes[i];
            left = std::min(left, b.x - x);
            top = std::min(top, b.y - y0);
            right = std::max(right, b.x - x + b.w);
            bottom = std::max(bottom, b.y - y0 + b.h);
        }
        return Rect{ left, top, right - left, bottom - top };
    }

    bool offscreen(const Rect& bounds, float x, float y) const {
        if (m_capturing > 0) return false;
        const Rect view = current_clip().intersect(Rect{ 0.0f, 0.0f, m_view_w, m_view_h });
        const float bx = x + bounds.x;
        const float by = y + bounds.y;
        return !(bx < view.x + view.w && bx + bounds.w > view.x && by < view.y + view.h && by + bounds.h > view.y);
    }

    void layout_dynamic(fizmo::graphics::Renderer& r, LayoutEntry& e, const Node& element, float x, float& y, float width, bool sized, float forced_h) {
        if (frame_stats_log().enabled) ++frame_stats_log().dynamic;
        const Mark before = mark();
        const float y0 = y;
        layout_element_now(r, element, x, y, width, sized, forced_h);
        e.advance = y - y0;
        e.bounds = slice_bounds(before, x, y0, e.advance);
        e.measured = true;
    }

    void capture_layout(LayoutEntry& e, const Mark& from, float x, float y0, float y1) {
        e.advance = y1 - y0;
        e.measured = true;
        e.ops.assign(m_ops.begin() + static_cast<std::ptrdiff_t>(from.ops), m_ops.end());
        e.boxes.assign(m_boxes.begin() + static_cast<std::ptrdiff_t>(from.boxes), m_boxes.end());
        e.runs.assign(m_runs.begin() + static_cast<std::ptrdiff_t>(from.runs), m_runs.end());
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = e.advance;
        for (DrawOp& op : e.ops) {
            op.x -= x;
            op.y -= y0;
            const float pad = op.kind == DrawOp::Kind::Circle || op.kind == DrawOp::Kind::CircleStroke ? op.radius : static_cast<float>(op.stroke) + 2.0f;
            left = std::min(left, op.x - pad);
            top = std::min(top, op.y - pad);
            right = std::max(right, op.x + op.w + pad);
            bottom = std::max(bottom, op.y + op.h + pad);
        }
        for (ElementBox& b : e.boxes) {
            b.x -= x;
            b.y -= y0;
            left = std::min(left, b.x);
            top = std::min(top, b.y);
            right = std::max(right, b.x + b.w);
            bottom = std::max(bottom, b.y + b.h);
        }
        for (TextRun& t : e.runs) {
            t.x -= x;
            t.y -= y0;
        }
        e.bounds = Rect{ left, top, right - left, bottom - top };
    }

    void replay_layout(const LayoutEntry& e, float x, float& y) {
        const Rect clip = current_clip();
        const Rect view = m_capturing == 0 ? clip.intersect(Rect{ 0.0f, 0.0f, m_view_w, m_view_h }) : k_everywhere;
        const float bx = x + e.bounds.x;
        const float by = y + e.bounds.y;
        const bool visible = m_capturing > 0 || (bx < view.x + view.w && bx + e.bounds.w > view.x && by < view.y + view.h && by + e.bounds.h > view.y);
        FrameStatsLog& stats = frame_stats_log();
        if (stats.enabled) ++(visible ? stats.replayed : stats.culled);
        if (visible) {
            const bool whole = m_capturing > 0 || (by >= view.y && by + e.bounds.h <= view.y + view.h);
            if (whole) m_ops.reserve(m_ops.size() + e.ops.size());
            const Rect band{ -k_far, view.y - y, 2.0f * k_far, view.h };
            for (const DrawOp& op : e.ops) {
                if (!whole && op.kind != DrawOp::Kind::ClipPush && op.kind != DrawOp::Kind::ClipPop && !op_visible(op, band)) continue;
                m_ops.push_back(op);
                m_ops.back().x += x;
                m_ops.back().y += y;
            }
            for (const ElementBox& b : e.boxes) {
                m_boxes.push_back(b);
                m_boxes.back().x += x;
                m_boxes.back().y += y;
                m_boxes.back().clip = clip;
            }
        }
        if (m_capturing == 0 && !e.runs.empty()) {
            m_run_refs.push_back(RunRef{ m_runs.size(), &e.runs, x, y, 0.0f, clip });
        } else {
            for (const TextRun& t : e.runs) {
                m_runs.push_back(t);
                m_runs.back().x += x;
                m_runs.back().y += y;
                m_runs.back().clip = clip;
            }
        }
        y += e.advance;
    }

    void prune_layout_cache() {
        if (m_frame_index % 120 != 0) return;
        if (m_layout_cache.size() >= 256) {
            for (auto it = m_layout_cache.begin(); it != m_layout_cache.end();) {
                if (it->second.used + 120 < m_frame_index) it = m_layout_cache.erase(it);
                else ++it;
            }
        }
        for (auto it = m_child_cache.begin(); it != m_child_cache.end();) {
            if (it->second.used + 120 < m_frame_index) it = m_child_cache.erase(it);
            else ++it;
        }
    }

    void layout_element(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, bool sized = false, float forced_h = -1.0f) {
        if (!m_layout_caching || m_sel_anchor.valid() || element.tag() == "window") {
            layout_element_now(r, element, x, y, width, sized, forced_h);
            return;
        }
        const std::uint64_t key = m_styles.style_key(element, m_project.diagnostics);
        const std::uint64_t generation = layout_generation();
        const bool wide = m_wide > 0;
        auto found = m_layout_cache.find(&element);
        if (found != m_layout_cache.end()) {
            LayoutEntry& e = found->second;
            e.used = m_frame_index;
            const bool same = e.stamp == element.tree_stamp() && e.style == key && e.generation == generation && e.width == width && e.avail == m_avail_h && e.forced == forced_h && e.sized == sized && e.wide == wide;
            if (same && !e.dynamic && e.captured) {
                e.streak = 0;
                replay_layout(e, x, y);
                return;
            }
            if (same && e.dynamic) {
                if (e.measured && !m_force_full && offscreen(e.bounds, x, y)) {
                    y += e.advance;
                    m_partial_runs = true;
                    return;
                }
                layout_dynamic(r, e, element, x, y, width, sized, forced_h);
                return;
            }
            bool volatile_entry = false;
            if (!same && !e.dynamic) {
                e.streak = e.changed_frame + 1 >= m_frame_index ? e.streak + 1 : 1;
                e.changed_frame = m_frame_index;
                volatile_entry = e.streak >= 3;
            }
            const bool retry_dynamic = e.dynamic && ++e.retries % 32 == 0;
            if (!same && ((e.dynamic && !retry_dynamic) || volatile_entry)) {
                e.stamp = element.tree_stamp();
                e.style = key;
                e.generation = generation;
                e.width = width;
                e.avail = m_avail_h;
                e.forced = forced_h;
                e.sized = sized;
                e.wide = wide;
                if (volatile_entry) {
                    e.captured = false;
                    e.ops.clear();
                    e.boxes.clear();
                    e.runs.clear();
                }
                layout_dynamic(r, e, element, x, y, width, sized, forced_h);
                return;
            }
        }
        const Mark before = mark();
        const float y0 = y;
        const float avail = m_avail_h;
        if (frame_stats_log().enabled) ++frame_stats_log().laid;
        ++m_capturing;
        layout_element_now(r, element, x, y, width, sized, forced_h);
        --m_capturing;
        LayoutEntry& e = m_layout_cache[&element];
        for (const RunRef& ref : m_run_refs) {
            if (ref.runs == &e.runs) {
                runs();
                break;
            }
        }
        e.stamp = element.tree_stamp();
        e.style = key;
        e.generation = generation;
        e.used = m_frame_index;
        e.width = width;
        e.avail = avail;
        e.forced = forced_h;
        e.sized = sized;
        e.wide = wide;
        e.dynamic = !captures_cleanly(before);
        if (e.dynamic) {
            e.ops.clear();
            e.boxes.clear();
            e.runs.clear();
            e.advance = y - y0;
            e.bounds = slice_bounds(before, x, y0, e.advance);
            e.measured = true;
        } else {
            capture_layout(e, before, x, y0, y);
            e.captured = true;
        }
    }

    void layout_element_now(fizmo::graphics::Renderer& r, const Node& element, float x, float& y, float width, bool sized = false, float forced_h = -1.0f) {
        if (element.tag() == "window") return;
        const bark::ComputedStyle& style = style_of(element);
        if (style.text_view("display") == "none") return;
        if (floating(style) && &element != m_floating_node) return;

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
            } else if (*kind == InputKind::Select) layout_select(r, element, bx, y, bw);
            else if (is_text_input(*kind) && element.tag() == "input_math") layout_math_field(r, element, bx, y, bw);
            else if (is_text_input(*kind)) layout_text_input(r, element, *kind, bx, y, bw);
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
            bw = m_wide > 0 ? iw + ipad.left + ipad.right : std::min(bw, iw + ipad.left + ipad.right);
        } else if (!sized) {
            if (const auto w = resolve_length(style, "width", width)) bw = m_wide > 0 ? *w : std::min(bw, *w);
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
        if (element.has_attribute("reveal")) {
            const std::string want = element.attribute_or("reveal", "");
            const auto done = m_revealed.find(&element);
            if (done == m_revealed.end() || done->second != want) {
                m_reveal_node = &element;
                m_reveal_y = top - margin.top;
                m_reveal_value = want;
            }
        }

        std::optional<float> fixed_h;
        if (forced_h >= 0.0f && element.tag() != "img") fixed_h = forced_h;
        else if (element.tag() == "img") fixed_h = std::nullopt;
        else if (const auto hgt = resolve_length(style, "height", m_avail_h)) fixed_h = *hgt;
        std::optional<float> max_h = resolve_length(style, "max-height", m_avail_h);
        const std::string_view overflow_raw = style.text_view("overflow");
        const std::string_view overflow = overflow_raw.empty() ? std::string_view("visible") : overflow_raw;
        const std::string_view overflow_y_raw = style.text_view("overflow-y");
        const std::string_view overflow_y = overflow_y_raw.empty() ? overflow : overflow_y_raw;
        const std::string_view overflow_x_raw = style.text_view("overflow-x");
        const std::string_view overflow_x = !overflow_x_raw.empty() ? overflow_x_raw : (overflow == "auto" || overflow == "scroll") ? std::string_view("hidden") : overflow;
        const bool clips = overflow_y == "hidden" || overflow_y == "scroll" || overflow_y == "auto" || overflow_x == "hidden" || overflow_x == "scroll" || overflow_x == "auto";
        const bool scrolls = overflow_y == "scroll" || overflow_y == "auto";
        const bool scrolls_x = overflow_x == "scroll" || overflow_x == "auto";

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
        const Mark inner = mark();
        if (scrolls_x) ++m_wide;
        if (element.tag() == "math") layout_math(element, content_x, cy, content_w);
        else if (element.tag() == "img") layout_image(element, content_x, cy, content_w);
        else if (element.tag() == "terminal") layout_terminal(r, element, content_x, cy, content_w, fixed_h ? std::max(0.0f, *fixed_h - pad.top - pad.bottom) : -1.0f);
        else if (element.tag() == "canvas") layout_canvas(element, content_x, cy, content_w, fixed_h ? std::max(0.0f, *fixed_h - pad.top - pad.bottom) : -1.0f);
        else if (element.tag() == "br") cy += static_cast<float>(font_size_of(element));
        else if (element.tag() == "hr") {
            push_rect(DrawOp::Kind::Fill, content_x, cy + 4, content_w, 1, to_color(style.color("color").value_or(bark::Rgba{ 120, 120, 120, 255 })));
            cy += 9;
        } else if (kind && *kind == InputKind::File) {
            layout_file_content(r, element, content_x, cy, content_w);
        } else lay_content(r, element, style, content_x, cy, content_w);
        if (scrolls_x) --m_wide;
        m_avail_h = saved_avail;

        const float natural_bottom = cy + scroll + pad.bottom;
        float bottom = natural_bottom;
        if (fixed_h) bottom = clips ? top + *fixed_h : std::max(bottom, top + *fixed_h);
        if (const auto mh = resolve_length(style, "min-height", saved_avail)) bottom = std::max(bottom, top + *mh);
        if (max_h && clips) bottom = std::min(bottom, top + *max_h);

        if (clips) {
            m_ops[clip_op].h = bottom - top;
            Rect vbar{ 0.0f, 0.0f, 0.0f, 0.0f };
            Rect hbar{ 0.0f, 0.0f, 0.0f, 0.0f };
            float max_scroll_x = 0.0f;
            if (scrolls_x) {
                float right = content_x + content_w;
                for (std::size_t i = inner.ops; i < m_ops.size(); ++i) {
                    const DrawOp& op = m_ops[i];
                    if (op.kind == DrawOp::Kind::ClipPush || op.kind == DrawOp::Kind::ClipPop) continue;
                    right = std::max(right, op.x + op.w + pad.right);
                }
                max_scroll_x = std::max(0.0f, right - (bx + bw));
                float& sx = m_elem_scroll_x[&element];
                sx = std::clamp(sx, 0.0f, max_scroll_x);
                if (sx > 0.0f) shift_x(inner, -sx);
                if (max_scroll_x > 0.0f) {
                    const float track = bw;
                    const float thumb = std::max(20.0f, track * track / (track + max_scroll_x));
                    const float tx = bx + (track - thumb) * (sx / max_scroll_x);
                    const bool held = (m_bar_drag.node == &element && m_bar_drag.horizontal) || (m_bar_hot.node == &element && m_bar_hot.horizontal);
                    push_rect(DrawOp::Kind::Fill, tx, bottom - (held ? 7 : 6), thumb, held ? 6 : 4, with_alpha(raw_text_color(element), held ? 0.45 : 0.25), 1, 2);
                    hbar = Rect{ bx, bottom - 10, track, thumb };
                }
            }
            float max_scroll = 0.0f;
            if (scrolls) {
                max_scroll = std::max(0.0f, natural_bottom - bottom);
                float& s = m_elem_scroll[&element];
                if (style.text_view("scroll-anchor") == "bottom") {
                    const auto known = m_elem_scroll_end.find(&element);
                    const bool pinned = known == m_elem_scroll_end.end() || s >= known->second - 2.0f;
                    m_elem_scroll_end[&element] = max_scroll;
                    if (pinned && s != max_scroll) {
                        s = max_scroll;
                        poke();
                    }
                }
                if (m_reveal_node) {
                    bool inside = false;
                    for (const Node* n = m_reveal_node->parent(); n; n = n->parent()) {
                        if (n == &element) {
                            inside = true;
                            break;
                        }
                    }
                    if (inside) {
                        s += m_reveal_y - (top + pad.top);
                        m_revealed[m_reveal_node] = m_reveal_value;
                        m_reveal_node = nullptr;
                        poke();
                    }
                }
                s = std::clamp(s, 0.0f, max_scroll);
                if (max_scroll > 0.0f) {
                    const float track = bottom - top;
                    const float thumb = std::max(20.0f, track * track / (track + max_scroll));
                    const float ty = top + (track - thumb) * (s / max_scroll);
                    const bool held = (m_bar_drag.node == &element && !m_bar_drag.horizontal) || (m_bar_hot.node == &element && !m_bar_hot.horizontal);
                    push_rect(DrawOp::Kind::Fill, bx + bw - (held ? 7 : 6), ty, held ? 6 : 4, thumb, with_alpha(raw_text_color(element), held ? 0.45 : 0.25), 1, 2);
                    vbar = Rect{ bx + bw - 12, top, track, thumb };
                }
            }
            if (scrolls || scrolls_x) {
                Scroller sc{ &element, current_clip(), max_scroll, false, max_scroll_x };
                if (vbar.h > 0.0f) {
                    sc.bar_x = vbar.x;
                    sc.track_y = vbar.y;
                    sc.track_h = vbar.w;
                    sc.thumb_h = vbar.h;
                }
                if (hbar.h > 0.0f) {
                    sc.track_x = hbar.x;
                    sc.bar_y = hbar.y;
                    sc.track_w = hbar.w;
                    sc.thumb_w = hbar.h;
                }
                m_scrollers.push_back(sc);
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
        bar_hover(px, py);
        update_cursor(target);
    }

    void update_cursor(const Node* target) {
        if (!m_native || !target) return;
        std::string name = style_of(*target).text("cursor").value_or("");
        if (m_code_hover.shown && m_code_hover.panel.contains(m_mouse_x, m_mouse_y)) {
            name = "default";
            for (const auto& [rect, action] : m_code_hover.actions) {
                if (!action.empty() && rect.contains(m_mouse_x, m_mouse_y)) name = "pointer";
            }
            if (name == m_cursor) return;
            m_cursor = name;
            m_native->set_cursor(cursor_for(name));
            return;
        }
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
        if (const std::optional<InputKind> kind = input_kind_of(*target); kind && *kind == InputKind::Slider && (name.empty() || name == "auto" || name == "default")) name = "pointer";
        if (is_disabled(target)) name = "default";
        if (m_bar_hot.node || m_bar_drag.node) name = "pointer";
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
        const bool was_term = old && old->tag() == "terminal" && document().is_attached(*old);
        const bool is_term = node && node->tag() == "terminal";
        if (is_term && m_native) m_native->start_text_input();
        else if (was_term && m_native && !m_focus && !m_code_focus) m_native->stop_text_input();
        if (was_term) {
            const std::shared_ptr<Terminal> term = terminal_of(document(), *old);
            term->send(term->encode_focus(false));
        }
        if (is_term) {
            const std::shared_ptr<Terminal> term = terminal_of(document(), *node);
            term->send(term->encode_focus(true));
        }
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
        m_edit_caret = m_edit.size();
        m_edit_scroll = 0.0f;
        m_edit_view = node;
        m_focus_start = m_edit;
        m_invalid = false;
        m_edit_anchor = std::string::npos;
        m_edit_undo.clear();
        m_edit_redo.clear();
        m_edit_undo_kind = 0;
        m_edit_all = input_kind_of(*node) == InputKind::Color && !m_edit.empty();
        if (m_native) m_native->start_text_input();
        set_focused(node);
    }

    bool commit_focus() {
        if (!m_focus) return true;
        Node& node = mutable_node(*m_focus);
        if (!commit_input_text(document(), node, m_edit)) return false;
        const std::string committed = input_text(node);
        if (committed != m_edit) m_edit_caret = committed.size();
        m_edit = committed;
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
        m_edit_caret = 0;
        m_edit_scroll = 0.0f;
        m_edit_click_x = -1.0f;
        m_edit_view = nullptr;
        m_invalid = false;
        m_edit_all = false;
        m_edit_anchor = std::string::npos;
        m_edit_dragging = false;
        m_edit_undo.clear();
        m_edit_redo.clear();
        if (m_native) m_native->stop_text_input();
    }

    void layout_floating(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        if (m_float_scan_stamp != m_window.tree_stamp() || m_float_scan_revision != document().revision()) {
            m_floats.clear();
            std::function<void(const Node&)> collect = [&](const Node& n) {
                for (const auto& c : n.children()) {
                    if (!c->is_element() || c->tag() == "window") continue;
                    const bark::ComputedStyle& style = style_of(*c);
                    if (style.text_view("display") == "none") continue;
                    if (floating(style)) {
                        if (is_page_shown(*c)) m_floats.push_back(c.get());
                        continue;
                    }
                    collect(*c);
                }
            };
            collect(m_window);
            m_float_scan_stamp = m_window.tree_stamp();
            m_float_scan_revision = document().revision();
        }
        const std::vector<const Node*> floats = m_floats;
        for (const Node* node : floats) {
            if (!document().is_attached(*node)) continue;
            const bark::ComputedStyle& style = style_of(*node);
            const bool cursor = style.text_view("position") == "cursor";
            const Box pad = box_of(style, "padding");
            m_floating_node = node;
            float width = 0.0f;
            if (const auto fixed = resolve_length(style, "width", view_w)) width = *fixed;
            else {
                const float natural = fit_width(r, *node, view_w);
                width = natural > 0.0f ? std::ceil(natural) : 320.0f;
            }
            if (const auto mn = resolve_length(style, "min-width", view_w)) width = std::max(width, *mn);
            if (const auto mx = resolve_length(style, "max-width", view_w)) width = std::min(width, *mx);
            width = std::max(1.0f, std::min(width, view_w));
            const float inner_w = std::max(1.0f, width - pad.left - pad.right);
            float max_h = view_h;
            if (const auto mx = resolve_length(style, "max-height", view_h)) max_h = std::min(max_h, *mx);
            const float saved_avail = m_avail_h;
            m_avail_h = std::max(0.0f, max_h - pad.top - pad.bottom);
            const Mark before = mark();
            float my = 0.0f;
            lay_content(r, *node, style, pad.left, my, inner_w);
            rewind(before);
            float height = my + pad.top + pad.bottom;
            if (const auto fixed = resolve_length(style, "height", view_h)) height = *fixed;
            if (const auto mn = resolve_length(style, "min-height", view_h)) height = std::max(height, *mn);
            height = std::min(height, max_h);
            const auto left = resolve_length(style, "left", view_w);
            const auto top = resolve_length(style, "top", view_h);
            const auto right = resolve_length(style, "right", view_w);
            const auto bottom = resolve_length(style, "bottom", view_h);
            float x = 0.0f;
            float y = 0.0f;
            if (cursor) {
                const float dx = left.value_or(14.0f);
                const float dy = top.value_or(18.0f);
                x = m_mouse_x + dx;
                y = m_mouse_y + dy;
                if (x + width > view_w) x = std::max(0.0f, m_mouse_x - width - 4.0f);
                if (y + height > view_h) y = std::max(0.0f, m_mouse_y - height - 4.0f);
            } else {
                x = left ? *left : right ? view_w - *right - width : 0.0f;
                y = top ? *top : bottom ? view_h - *bottom - height : 0.0f;
            }
            x = std::round(x);
            y = std::round(y);
            const Mark start = mark();
            const float radius = static_cast<float>(style.length("border-radius").value_or(0.0));
            if (auto bg = style.color("background")) push_rect(DrawOp::Kind::Fill, x, y, width, height, to_color(*bg), 1, radius);
            if (auto border = style.color("border-color")) push_rect(DrawOp::Kind::Stroke, x, y, width, height, to_color(*border), static_cast<unsigned>(style.length("border-width").value_or(1.0)), radius);
            add_box(ElementBox{ node, x, y, width, height });
            push_clip(Rect{ x, y, width, height });
            float cy = y + pad.top;
            lay_content(r, *node, style, x + pad.left, cy, inner_w);
            pop_clip();
            m_avail_h = saved_avail;
            m_floating_node = nullptr;
            if (cursor) {
                m_hits.resize(start.hits);
                m_boxes.resize(start.boxes);
            }
        }
    }

    void layout_modals(fizmo::graphics::Renderer& r, float view_w, float view_h) {
        const Node* was = m_modal;
        m_modal = nullptr;
        m_modal_boxes = 0;
        m_modal_hits = 0;
        if (m_modal_scan_stamp != m_window.tree_stamp()) {
            m_open_modals.clear();
            std::function<void(const Node&)> collect = [&](const Node& n) {
                for (const auto& c : n.children()) {
                    if (!c->is_element() || c->tag() == "window") continue;
                    if (is_modal(*c) && c->has_attribute("open") && is_page_shown(*c)) m_open_modals.push_back(c.get());
                    collect(*c);
                }
            };
            collect(m_window);
            m_modal_scan_stamp = m_window.tree_stamp();
            m_modal_scan_revision = document().revision();
        }
        const std::vector<const Node*> open = m_open_modals;
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
        if (completion_click(px, py)) return;
        if (code_hover_click(px, py)) return;
        hide_code_hover();
        if (m_menu.node) {
            if (m_menu.panel.contains(px, py)) {
                for (const auto& [row, index] : m_menu.rows) {
                    if (row.contains(px, py) && py >= m_menu.panel.y + 4 && py < m_menu.panel.y + m_menu.panel.h - 4) {
                        const std::vector<SelectOption> options = select_options(*m_menu.node);
                        const bool disabled = index < options.size() && options[index].disabled;
                        choose_option(index);
                        if (!disabled) close_menu();
                        poke();
                        return;
                    }
                }
                return;
            }
            const Node* was = m_menu.node;
            const bool on_anchor = m_menu.anchor.contains(px, py);
            close_menu();
            if (on_anchor) {
                set_focused(was);
                return;
            }
        }
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
        if ((!m_modal || m_modal_panel.contains(px, py)) && bar_press(px, py)) return;
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
        m_sel_pending = false;
        m_sel_dragged = false;
        if (allowed && selectable_target(target)) {
            const TextPoint at = point_at(px, py, false);
            const bool extend = (m_mods & ModifierShift) != 0 && m_sel_anchor.valid();
            if (at.valid()) {
                if (extend) {
                    m_sel_focus = at;
                    m_sel_dragged = true;
                } else {
                    m_sel_anchor = at;
                    m_sel_focus = at;
                }
                m_sel_pending = true;
                m_sel_press_x = px;
                m_sel_press_y = py;
                poke();
            } else if (!extend) {
                clear_text_selection();
            }
        } else if (allowed) {
            clear_text_selection();
        }

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
                        bool shift = (m_mods & ModifierShift) != 0;
                        bool alt = (m_mods & ModifierAlt) != 0;
                        bool ctrl = (m_mods & (ModifierControl | ModifierMeta)) != 0;
                        bool column_ok = true;
                        if (shift || alt || ctrl) {
                            const Keymap overrides = parse_keymap(hit.node->attribute_or("keys", ""));
                            const Chord pressed = chord_of("Click", m_mods);
                            const std::optional<std::string> action = code_action_for(overrides, pressed);
                            const KeyAction* bound = action ? find_code_editor_action(*action) : nullptr;
                            if (bound && bound->mouse && !bound->defaults.empty()) {
                                const Chord& d = bound->defaults.front();
                                ctrl = d.ctrl;
                                shift = d.shift;
                                alt = d.alt;
                                column_ok = bound->id == "column-select" || !shift;
                            } else if (is_code_default_chord(pressed)) {
                                column_ok = false;
                                ctrl = false;
                                alt = false;
                            }
                        }
                        ed.column_mode = false;
                        ed.box_mode = false;
                        ed.reveal = true;
                        if (ctrl && alt && !shift) {
                            const float cell = std::max(1.0f, ed.char_w);
                            const float fx = (px - ed.text_x + ed.scroll_x) / cell;
                            ed.box_mode = true;
                            ed.box_line = at.line;
                            ed.box_column = fx <= 0.0f ? 0 : static_cast<std::size_t>(std::lround(fx));
                            ed.buffer.box_select(ed.box_line, ed.box_column, ed.box_line, ed.box_column, ed.tab_size);
                            m_code_drag = hit.node;
                            return;
                        }
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
                        if (shift && column_ok && !ed.buffer.has_selection() && !ed.buffer.has_extra_cursors()) {
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
                    case InputKind::Select:
                        blur(true);
                        blur_code();
                        set_focused(hit.node);
                        open_menu(hit.node);
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
                        if (is_text_input(hit.kind) && hit.kind != InputKind::Color) {
                            const bool extend = (m_mods & ModifierShift) != 0 && m_focus == hit.node;
                            focus(hit.node);
                            m_edit_click_x = px;
                            m_edit_click_y = py;
                            m_edit_click_extend = extend;
                            m_edit_dragging = true;
                            m_edit_all = false;
                            mutable_node(*hit.node).mark_changed(false);
                            poke();
                            return;
                        }
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
        if (button == 1 && m_bar_drag.node) {
            if (document().is_attached(*m_bar_drag.node)) mutable_node(*m_bar_drag.node).mark_changed(false);
            m_bar_drag = BarDrag{};
            hover_at(px, py);
            poke();
            return;
        }
        term_mouse_up(px, py, button);
        if (button == 1) m_edit_dragging = false;
        const bool selected_by_drag = button == 1 && m_sel_pending && m_sel_dragged && has_text_selection();
        if (button == 1) m_sel_pending = false;
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
        if (selected_by_drag) return;
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
        if (m_swallow_text) {
            m_swallow_text = false;
            return;
        }
        Event e;
        e.type = "text";
        e.text = text;
        if (!dispatch(key_target(), e)) return;
        if (!m_focus && !m_code_focus && m_focused && m_focused->tag() == "terminal" && document().is_attached(*m_focused)) {
            if (m_term_swallow) {
                m_term_swallow = false;
                return;
            }
            term_send(*m_focused, text);
            return;
        }
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
            after_code_typing(m_code_focus, clean);
            return;
        }
        if (!m_focus) return;
        if (m_focus->tag() == "input_math" && math_type(text)) return;
        edit_insert(text);
        if (m_focus && m_focus->tag() == "input_math") math_after_type(text);
    }

    static bool math_word_char(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    static std::size_t math_radical_before(const std::string& text, std::size_t end) {
        for (const char* name : { "sqrt", "cbrt" }) {
            const std::size_t n = std::char_traits<char>::length(name);
            if (end < n || text.compare(end - n, n, name) != 0) continue;
            const std::size_t start = end - n;
            if (start > 0 && math_word_char(text[start - 1])) continue;
            return start;
        }
        return std::string::npos;
    }

    static std::size_t math_matching_close(const std::string& text, std::size_t open) {
        int depth = 0;
        for (std::size_t i = open; i < text.size(); ++i) {
            if (text[i] == '(') ++depth;
            else if (text[i] == ')' && --depth == 0) return i;
        }
        return std::string::npos;
    }

    static std::size_t math_matching_open(const std::string& text, std::size_t close) {
        int depth = 0;
        for (std::size_t i = close + 1; i > 0; --i) {
            const char c = text[i - 1];
            if (c == ')' || c == '}' || c == ']') ++depth;
            else if ((c == '(' || c == '{' || c == '[') && --depth == 0) return i - 1;
        }
        return std::string::npos;
    }

    static std::size_t math_matching_brace(const std::string& text, std::size_t open) {
        int depth = 0;
        for (std::size_t i = open; i < text.size(); ++i) {
            if (text[i] == '{') ++depth;
            else if (text[i] == '}' && --depth == 0) return i;
        }
        return std::string::npos;
    }

    std::size_t math_numerator_start(std::size_t caret) const {
        std::size_t j = caret;
        int depth = 0;
        while (j > 0) {
            const char c = m_edit[j - 1];
            if (c == ')' || c == '}' || c == ']') {
                ++depth;
            } else if (c == '(' || c == '{' || c == '[') {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (std::strchr("+-=<>,;:| ", c) != nullptr)) {
                break;
            }
            --j;
        }
        return j;
    }

    void math_changed(const std::string& typed) {
        m_invalid = false;
        Event input;
        input.type = "input";
        input.text = typed;
        input.edit_text = m_edit;
        input.has_edit_text = true;
        dispatch(m_focus, input);
    }

    bool math_type(const std::string& text) {
        std::size_t a = 0;
        std::size_t b = 0;
        if (edit_range(a, b)) return false;
        const std::size_t caret = std::min(m_edit_caret, m_edit.size());
        if (text == ")" && caret < m_edit.size() && m_edit[caret] == ')') {
            const std::size_t open = math_matching_open(m_edit, caret);
            if (open != std::string::npos && math_radical_before(m_edit, open) != std::string::npos) {
                m_edit_caret = caret + 1;
                return true;
            }
            return false;
        }
        if (text == "^" || text == "_") {
            if (!input_accepts(*m_focus, m_edit.substr(0, caret), text)) return false;
            edit_remember(0);
            m_edit.insert(caret, text + "{}");
            m_edit_caret = caret + 2;
            math_changed(text);
            return true;
        }
        if (text == "/") {
            if (!input_accepts(*m_focus, m_edit.substr(0, caret), text)) return false;
            edit_remember(0);
            const std::size_t start = math_numerator_start(caret);
            const std::string num = m_edit.substr(start, caret - start);
            m_edit.replace(start, caret - start, "{" + num + "}/{}");
            m_edit_caret = num.empty() ? start + 1 : start + num.size() + 4;
            math_changed(text);
            return true;
        }
        return false;
    }

    void math_after_type(const std::string& text) {
        if (text.empty() || !math_word_char(text.back())) return;
        const std::size_t caret = std::min(m_edit_caret, m_edit.size());
        if (math_radical_before(m_edit, caret) == std::string::npos) return;
        if (caret < m_edit.size() && (m_edit[caret] == '(' || math_word_char(m_edit[caret]))) return;
        if (!input_accepts(*m_focus, m_edit.substr(0, caret), "(")) return;
        m_edit.insert(caret, "()");
        m_edit_caret = caret + 1;
        math_changed("()");
    }

    bool math_unwrap_fraction(std::size_t num_open, std::size_t& caret) {
        const std::size_t num_close = math_matching_brace(m_edit, num_open);
        if (num_close == std::string::npos || num_close + 2 >= m_edit.size() || m_edit[num_close + 1] != '/' || m_edit[num_close + 2] != '{') return false;
        const std::size_t den_close = math_matching_brace(m_edit, num_close + 2);
        if (den_close != std::string::npos) m_edit.erase(den_close, 1);
        m_edit.erase(num_close, 3);
        m_edit.erase(num_open, 1);
        caret = num_close - 1;
        return true;
    }

    bool math_backspace() {
        const std::size_t caret = std::min(m_edit_caret, m_edit.size());
        if (caret == 0) return false;
        const char before = m_edit[caret - 1];
        if (before == '}') {
            m_edit_caret = caret - 1;
            return true;
        }
        if (before == '{') {
            const std::size_t open = caret - 1;
            if (open > 0 && (m_edit[open - 1] == '^' || m_edit[open - 1] == '_')) {
                const std::size_t close = math_matching_brace(m_edit, open);
                if (close != std::string::npos) m_edit.erase(close, 1);
                m_edit.erase(open - 1, 2);
                m_edit_caret = open - 1;
                return true;
            }
            if (open >= 2 && m_edit[open - 1] == '/' && m_edit[open - 2] == '}') {
                const std::size_t num_open = math_matching_open(m_edit, open - 2);
                std::size_t at = caret;
                if (num_open != std::string::npos && math_unwrap_fraction(num_open, at)) {
                    m_edit_caret = at;
                    return true;
                }
            }
            std::size_t at = caret;
            if (math_unwrap_fraction(open, at)) {
                m_edit_caret = open;
                return true;
            }
            const std::size_t close = math_matching_brace(m_edit, open);
            if (close != std::string::npos) m_edit.erase(close, 1);
            m_edit.erase(open, 1);
            m_edit_caret = open;
            return true;
        }
        if (before != '(') return false;
        const std::size_t start = math_radical_before(m_edit, caret - 1);
        if (start == std::string::npos) return false;
        const std::size_t close = math_matching_close(m_edit, caret - 1);
        if (close != std::string::npos) m_edit.erase(close, 1);
        m_edit.erase(start, caret - start);
        m_edit_caret = start;
        return true;
    }

    bool edit_range(std::size_t& a, std::size_t& b) const {
        if (m_edit_all && !m_edit.empty()) {
            a = 0;
            b = m_edit.size();
            return true;
        }
        if (m_edit_anchor == std::string::npos) return false;
        const std::size_t c = std::min(m_edit_caret, m_edit.size());
        const std::size_t n = std::min(m_edit_anchor, m_edit.size());
        if (c == n) return false;
        a = std::min(c, n);
        b = std::max(c, n);
        return true;
    }

    void edit_apply_click(std::size_t previous) {
        if (m_edit_click_extend) {
            if (m_edit_anchor == std::string::npos) m_edit_anchor = previous;
        } else {
            m_edit_anchor = m_edit_caret;
        }
        if (m_edit_anchor == m_edit_caret && !m_edit_dragging) m_edit_anchor = std::string::npos;
        m_edit_click_x = -1.0f;
        m_edit_click_y = -1.0f;
        m_edit_click_extend = false;
        m_edit_all = false;
    }

    void edit_clear_selection() {
        m_edit_all = false;
        m_edit_anchor = std::string::npos;
    }

    void edit_remember(int kind) {
        const auto now = std::chrono::steady_clock::now();
        const bool merge = kind != 0 && kind == m_edit_undo_kind && now - m_edit_undo_time < std::chrono::milliseconds(900);
        m_edit_undo_time = now;
        m_edit_undo_kind = kind;
        m_edit_redo.clear();
        if (merge) return;
        if (!m_edit_undo.empty() && m_edit_undo.back().first == m_edit) return;
        m_edit_undo.emplace_back(m_edit, m_edit_caret);
        if (m_edit_undo.size() > 200) m_edit_undo.erase(m_edit_undo.begin());
    }

    void edit_fire_input() {
        if (!m_focus) return;
        m_invalid = false;
        Event input;
        input.type = "input";
        input.edit_text = m_edit;
        input.has_edit_text = true;
        dispatch(m_focus, input);
    }

    bool edit_undo(bool redo) {
        if (!m_focus || m_focus->has_attribute("readonly")) return false;
        auto& from = redo ? m_edit_redo : m_edit_undo;
        auto& to = redo ? m_edit_undo : m_edit_redo;
        while (!from.empty() && from.back().first == m_edit) from.pop_back();
        if (from.empty()) return false;
        to.emplace_back(m_edit, m_edit_caret);
        m_edit = from.back().first;
        m_edit_caret = std::min(from.back().second, m_edit.size());
        from.pop_back();
        m_edit_undo_kind = 0;
        edit_clear_selection();
        edit_fire_input();
        return true;
    }

    bool edit_delete_selection() {
        std::size_t a = 0;
        std::size_t b = 0;
        if (!edit_range(a, b)) return false;
        m_edit.erase(a, b - a);
        m_edit_caret = a;
        edit_clear_selection();
        return true;
    }

    void edit_insert(const std::string& text) {
        if (!m_focus) return;
        bool changed = false;
        edit_remember(1);
        {
            std::size_t a = 0;
            std::size_t b = 0;
            const std::vector<std::string> pieces = split_code_points(text);
            if (edit_range(a, b) && !pieces.empty() && input_accepts(*m_focus, m_edit.substr(0, a), pieces.front())) changed = edit_delete_selection();
            edit_clear_selection();
        }
        for (const std::string& cp : split_code_points(text)) {
            m_edit_caret = std::min(m_edit_caret, m_edit.size());
            if (input_accepts(*m_focus, m_edit.substr(0, m_edit_caret), cp)) {
                m_edit.insert(m_edit_caret, cp);
                m_edit_caret += cp.size();
                m_invalid = false;
                changed = true;
            }
        }
        if (changed) {
            Event input;
            input.type = "input";
            input.text = text;
            input.edit_text = m_edit;
            input.has_edit_text = true;
            dispatch(m_focus, input);
        }
    }

    static std::string one_line_paste(std::string text) {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        std::string out;
        out.reserve(text.size());
        for (char c : text) {
            if (c == '\r') continue;
            out += (c == '\n' || c == '\t') ? ' ' : c;
        }
        return out;
    }

    bool edit_copy(bool cut) {
        if (!m_focus || !m_native) return false;
        if (m_focus->has_attribute("masked") || m_edit.empty()) return false;
        std::size_t a = 0;
        std::size_t b = 0;
        if (m_edit_anchor != std::string::npos && !m_edit_all && edit_range(a, b)) {
            put_clipboard(m_native, m_edit.substr(a, b - a));
            if (!cut || is_disabled(m_focus) || m_focus->has_attribute("readonly")) return true;
            edit_remember(0);
            edit_delete_selection();
            edit_fire_input();
            return true;
        }
        put_clipboard(m_native, m_edit);
        if (!cut || is_disabled(m_focus) || m_focus->has_attribute("readonly")) return true;
        edit_remember(0);
        m_edit.clear();
        m_edit_caret = 0;
        m_edit_all = false;
        m_invalid = false;
        Event input;
        input.type = "input";
        input.edit_text = m_edit;
        input.has_edit_text = true;
        dispatch(m_focus, input);
        return true;
    }

    bool edit_paste() {
        if (!m_focus || !m_native || is_disabled(m_focus) || m_focus->has_attribute("readonly")) return false;
        const std::string text = one_line_paste(read_clipboard(m_native));
        if (text.empty()) return false;
        edit_insert(text);
        return true;
    }

    bool field_menu(const Node* under, float px, float py) {
        const Node* field = under;
        while (field && !(input_kind_of(*field) && is_text_input(*input_kind_of(*field)))) field = field->parent();
        if (!field || is_disabled(field) || m_context.owner) return false;
        if (m_focus != field) {
            focus(field);
            m_edit_all = !m_edit.empty();
        }
        const bool hidden = field->has_attribute("masked");
        const bool locked = field->has_attribute("readonly");
        const bool has_text = !m_edit.empty();
        std::vector<MenuItem> items;
        items.push_back(MenuItem{ "grove:cut", "Cut", "Ctrl+X", hidden || locked || !has_text, false });
        items.push_back(MenuItem{ "grove:copy", "Copy", "Ctrl+C", hidden || !has_text, false });
        items.push_back(MenuItem{ "grove:paste", "Paste", "Ctrl+V", locked || !m_native, false });
        items.push_back(MenuItem{ "", "", "", false, true });
        items.push_back(MenuItem{ "grove:select-all", "Select All", "Ctrl+A", !has_text, false });
        return open_context_menu(mutable_node(*field), items, px, py);
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
        if (m_context.owner) {
            const Key k = e.logical_key != Key::Unknown ? e.logical_key : e.physical_key;
            if (k == Key::Escape || k == Key::Tab) close_context_menu(true);
            else if (k == Key::Up) step_context(-1);
            else if (k == Key::Down) step_context(1);
            else if (k == Key::Enter || k == Key::Space) {
                if (context_selectable(m_context.active)) choose_context(m_context.active);
            }
            poke();
            return;
        }
        Event ev = key_event("key_down", e);
        m_swallow_text = false;
        const Node* editing = m_focus && input_kind_of(*m_focus) == InputKind::String ? m_focus : nullptr;
        std::string synced;
        if (editing) {
            Node& field = mutable_node(*editing);
            if (input_text(field) != m_edit) set_input_string(document(), field, m_edit);
            synced = input_text(field);
        }
        const bool allowed = dispatch(key_target(), ev);
        if (editing && m_focus == editing && document().is_attached(*editing)) {
            const std::string now = input_text(*editing);
            if (now != synced) {
                m_edit = now;
                m_edit_caret = now.size();
                m_focus_start = now;
                m_edit_all = false;
                m_invalid = false;
            }
        }
        if (!allowed) {
            m_swallow_text = true;
            return;
        }
        if (!m_focus && !m_code_focus && m_focused && m_focused->tag() == "terminal" && document().is_attached(*m_focused)) {
            terminal_key(*m_focused, e);
            return;
        }
        if (!m_focus && !m_code_focus && has_text_selection()) {
            const Key k = e.logical_key != Key::Unknown ? e.logical_key : e.physical_key;
            const bool ctrl = fizmo::input::has(e.mods, fizmo::input::Modifiers::Control) || fizmo::input::has(e.mods, fizmo::input::Modifiers::Meta);
            if (ctrl && (k == Key::C || k == Key::Insert)) {
                copy_text_selection();
                return;
            }
            if (k == Key::Escape) {
                clear_text_selection();
                return;
            }
        }
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
            if (completion_key(key, e.mods)) return;
            code_key(key, e.mods);
            if (m_code_focus) after_code_key(key);
            return;
        }
        if (!m_focus && m_menu.node) {
            const std::size_t count = select_options(*m_menu.node).size();
            if (key == Key::Escape || key == Key::Tab) close_menu();
            else if (key == Key::Up || key == Key::Down) {
                m_menu.active = step_option(*m_menu.node, m_menu.active, key == Key::Up ? -1 : 1);
                m_menu.reveal = true;
            } else if (key == Key::Home || key == Key::End) {
                m_menu.active = key == Key::Home ? 0 : (count == 0 ? 0 : count - 1);
                m_menu.reveal = true;
            } else if (key == Key::PageUp || key == Key::PageDown) {
                for (int i = 0; i < 10; ++i) m_menu.active = step_option(*m_menu.node, m_menu.active, key == Key::PageUp ? -1 : 1);
                m_menu.reveal = true;
            } else if (key == Key::Enter || key == Key::Space) {
                choose_option(m_menu.active);
                close_menu();
            }
            return;
        }
        if (!m_focus && m_focused && input_kind_of(*m_focused) == InputKind::Select && !is_disabled(m_focused)) {
            if (key == Key::Enter || key == Key::Space) open_menu(m_focused);
            else if (key == Key::Up || key == Key::Down) {
                const std::size_t from = select_index(*m_focused).value_or(0);
                const std::size_t to = step_option(*m_focused, from, key == Key::Up ? -1 : 1);
                if (to != from || !select_index(*m_focused)) choose_option(to);
            }
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
            m_swallow_text = true;
            return;
        }
        {
            const bool ctrl_key = fizmo::input::has(e.mods, fizmo::input::Modifiers::Control) || fizmo::input::has(e.mods, fizmo::input::Modifiers::Meta);
            const bool shift_key = fizmo::input::has(e.mods, fizmo::input::Modifiers::Shift);
            if ((ctrl_key && key == Key::V) || (shift_key && !ctrl_key && key == Key::Insert)) {
                edit_paste();
                m_swallow_text = true;
                return;
            }
            if (ctrl_key && (key == Key::C || key == Key::Insert)) {
                edit_copy(false);
                m_swallow_text = true;
                return;
            }
            if ((ctrl_key && key == Key::X) || (shift_key && !ctrl_key && key == Key::Delete)) {
                edit_copy(true);
                m_swallow_text = true;
                return;
            }
            if (ctrl_key && (key == Key::Z || key == Key::Y)) {
                edit_undo(key == Key::Y || shift_key);
                m_swallow_text = true;
                return;
            }
            if (shift_key && (key == Key::Left || key == Key::Right || key == Key::Home || key == Key::End)) {
                if (m_edit_all) {
                    m_edit_anchor = key == Key::Left || key == Key::Home ? m_edit.size() : 0;
                    m_edit_caret = key == Key::Left || key == Key::Home ? m_edit.size() : 0;
                    m_edit_all = false;
                }
                if (m_edit_anchor == std::string::npos) m_edit_anchor = std::min(m_edit_caret, m_edit.size());
                const bool word = ctrl_key || fizmo::input::has(e.mods, fizmo::input::Modifiers::Alt);
                m_edit_caret = std::min(m_edit_caret, m_edit.size());
                const bool math_steps = !word && math_layout_current();
                if (key == Key::Left) m_edit_caret = word ? word_left(m_edit, m_edit_caret) : math_steps ? math_input_left(m_math_layout, m_edit_caret) : previous_code_point(m_edit, m_edit_caret);
                else if (key == Key::Right) m_edit_caret = word ? word_right(m_edit, m_edit_caret) : math_steps ? math_input_right(m_math_layout, m_edit_caret, m_edit.size()) : next_code_point(m_edit, m_edit_caret);
                else if (key == Key::Home) m_edit_caret = 0;
                else m_edit_caret = m_edit.size();
                return;
            }
        }
        std::size_t sel_a = 0;
        std::size_t sel_b = 0;
        const bool had_range = m_edit_anchor != std::string::npos && edit_range(sel_a, sel_b);
        if (key == Key::Left || key == Key::Right || key == Key::Home || key == Key::End || key == Key::Up || key == Key::Down) m_edit_anchor = std::string::npos;
        if (had_range && (key == Key::Backspace || key == Key::Delete)) {
            edit_remember(0);
            edit_delete_selection();
            edit_fire_input();
            return;
        }
        if (had_range && (key == Key::Left || key == Key::Right)) {
            m_edit_caret = key == Key::Left ? sel_a : sel_b;
            m_edit_all = false;
            return;
        }
        const bool was_all = m_edit_all;
        if (key == Key::Backspace || key == Key::Delete || key == Key::Up || key == Key::Down || key == Key::Left || key == Key::Right || key == Key::Home || key == Key::End) m_edit_all = false;
        const bool by_word = fizmo::input::has(e.mods, fizmo::input::Modifiers::Control) || fizmo::input::has(e.mods, fizmo::input::Modifiers::Alt);
        m_edit_caret = std::min(m_edit_caret, m_edit.size());
        const bool math_steps = !by_word && !was_all && math_layout_current();
        if (math_steps && (key == Key::Up || key == Key::Down)) {
            const std::size_t to = math_input_vertical(m_math_layout, m_edit_caret, key == Key::Up ? -1 : 1);
            if (to != std::string::npos) {
                m_edit_caret = std::min(to, m_edit.size());
                return;
            }
        }
        switch (key) {
            case Key::Left:
                if (was_all) m_edit_caret = 0;
                else m_edit_caret = by_word ? word_left(m_edit, m_edit_caret) : math_steps ? math_input_left(m_math_layout, m_edit_caret) : previous_code_point(m_edit, m_edit_caret);
                break;
            case Key::Right:
                if (was_all) m_edit_caret = m_edit.size();
                else m_edit_caret = by_word ? word_right(m_edit, m_edit_caret) : math_steps ? math_input_right(m_math_layout, m_edit_caret, m_edit.size()) : next_code_point(m_edit, m_edit_caret);
                break;
            case Key::Home:
                m_edit_caret = 0;
                break;
            case Key::End:
                m_edit_caret = m_edit.size();
                break;
            case Key::Delete:
            case Key::Backspace: {
                const std::string before = m_edit;
                edit_remember(2);
                if (was_all) {
                    m_edit.clear();
                    m_edit_caret = 0;
                } else if (key == Key::Backspace && !by_word && m_focus->tag() == "input_math" && math_backspace()) {
                } else if (key == Key::Backspace && m_edit_caret > 0) {
                    const std::size_t from = by_word ? word_left(m_edit, m_edit_caret) : previous_code_point(m_edit, m_edit_caret);
                    m_edit.erase(from, m_edit_caret - from);
                    m_edit_caret = from;
                } else if (key == Key::Delete && m_edit_caret < m_edit.size()) {
                    const std::size_t to = by_word ? word_right(m_edit, m_edit_caret) : next_code_point(m_edit, m_edit_caret);
                    m_edit.erase(m_edit_caret, to - m_edit_caret);
                }
                if (m_edit != before) {
                    m_invalid = false;
                    Event input;
                    input.type = "input";
                    input.edit_text = m_edit;
                    input.has_edit_text = true;
                    dispatch(m_focus, input);
                }
                break;
            }
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
                        m_edit_caret = m_edit.size();
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

    float& bar_scroll(const BarDrag& d) {
        if (d.code) {
            CodeEditor& ed = m_code[d.node];
            return d.horizontal ? ed.scroll_x : ed.scroll_y;
        }
        return d.horizontal ? m_elem_scroll_x[d.node] : m_elem_scroll[d.node];
    }

    bool bar_at(float px, float py, BarDrag& d) const {
        for (const Scroller& sc : m_scrollers) {
            if (!sc.rect.contains(px, py)) continue;
            d = BarDrag{};
            d.node = sc.node;
            d.code = sc.code;
            if (sc.thumb_h > 0.0f && sc.max > 0.0f && px >= sc.bar_x && py >= sc.track_y && py <= sc.track_y + sc.track_h) {
                d.start = sc.track_y;
                d.length = sc.track_h;
                d.thumb = sc.thumb_h;
                d.max = sc.max;
                return true;
            }
            if (sc.thumb_w > 0.0f && sc.max_x > 0.0f && py >= sc.bar_y && px >= sc.track_x && px <= sc.track_x + sc.track_w) {
                d.horizontal = true;
                d.start = sc.track_x;
                d.length = sc.track_w;
                d.thumb = sc.thumb_w;
                d.max = sc.max_x;
                return true;
            }
        }
        return false;
    }

    void bar_hover(float px, float py) {
        BarDrag d;
        const bool on = !m_bar_drag.node && bar_at(px, py, d);
        const Node* node = on ? d.node : nullptr;
        if (node == m_bar_hot.node && (!on || d.horizontal == m_bar_hot.horizontal)) return;
        if (m_bar_hot.node && document().is_attached(*m_bar_hot.node)) mutable_node(*m_bar_hot.node).mark_changed(false);
        m_bar_hot = on ? d : BarDrag{};
        if (node) mutable_node(*node).mark_changed(false);
        poke();
    }

    bool bar_press(float px, float py) {
        {
            BarDrag d;
            if (!bar_at(px, py, d)) return false;
            const float pos = d.horizontal ? px : py;
            const float span = std::max(1.0f, d.length - d.thumb);
            const float thumb_at = d.start + span * (bar_scroll(d) / d.max);
            if (pos >= thumb_at && pos <= thumb_at + d.thumb) {
                d.grab = pos - thumb_at;
            } else {
                d.grab = d.thumb / 2.0f;
            }
            m_bar_drag = d;
            m_bar_hot = BarDrag{};
            m_sel_pending = false;
            if (m_native && m_cursor != "pointer") {
                m_cursor = "pointer";
                m_native->set_cursor(cursor_for(m_cursor));
            }
            bar_drag_to(px, py);
            return true;
        }
    }

    void bar_drag_to(float px, float py) {
        BarDrag& d = m_bar_drag;
        if (!d.node || !document().is_attached(*d.node)) {
            m_bar_drag = BarDrag{};
            return;
        }
        const float pos = d.horizontal ? px : py;
        const float span = std::max(1.0f, d.length - d.thumb);
        const float t = std::clamp((pos - d.grab - d.start) / span, 0.0f, 1.0f);
        bar_scroll(d) = t * d.max;
        poke();
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
            if (horizontal) {
                if (sc.max_x <= 0.0f) continue;
                float& sx = m_elem_scroll_x[sc.node];
                const float before_x = sx;
                sx = std::clamp(sx - amount, 0.0f, sc.max_x);
                if (sx != before_x || amount != 0.0f) return true;
                continue;
            }
            if (sc.max <= 0.0f) continue;
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
        h.scroll = 0;
        h.max_scroll = 0;
        h.shown_text.clear();
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
        const std::string body = expand_tab_lines(attr.substr(nl + 1), ed.tab_size);
        if (body.find_first_not_of(" \n\t") == std::string::npos) return;
        if (body != h.shown_text) {
            h.shown_text = body;
            h.scroll = 0;
        }
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
        mono.set_strikethrough(false);
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
        const float wanted_w = static_cast<float>(std::max<std::int64_t>(160, parse_integer(h.node->attribute_or("hover-width", "")).value_or(900)));
        const float max_w = std::min(wanted_w, std::max(160.0f, view_w - 24.0f));
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
                const std::size_t lead = std::min(shown.find_first_not_of(' '), shown.size());
                const float indent = text_width(r, shown.substr(0, lead) + "    ", mono);
                float x = 0;
                for (const CodeToken& t : highlight_code_line(code_lang, shown, state)) {
                    std::string piece = shown.substr(t.begin, t.end - t.begin);
                    float w = text_width(r, piece, mono);
                    if (x + w > max_w && x > indent) {
                        widest = std::max(widest, x);
                        y += mono_h;
                        x = indent;
                        const std::size_t keep = piece.find_first_not_of(' ');
                        if (keep == std::string::npos) continue;
                        piece = piece.substr(keep);
                        w = text_width(r, piece, mono);
                    }
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
                    if (look.underline) it.style.set_underline(true);
                    if (look.strikethrough) it.style.set_strikethrough(true);
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
        const std::optional<std::int64_t> wanted_h = parse_integer(h.node->attribute_or("hover-height", ""));
        const float limit_h = wanted_h && *wanted_h > 0 ? static_cast<float>(std::max<std::int64_t>(60, *wanted_h)) : view_h * 0.6f;
        const float panel_h = std::min(std::min(limit_h, std::max(60.0f, view_h - 8.0f)), y + pad * 2.0f);
        h.max_scroll = std::max(0.0f, y + pad * 2.0f - panel_h);
        h.scroll = std::clamp(h.scroll, 0.0f, h.max_scroll);
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
            const float iy = py + pad + it.y - h.scroll;
            if (iy + it.h < py || iy > py + panel_h) continue;
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
        if (h.max_scroll > 0.0f) {
            const float track = panel_h - 8.0f;
            const float thumb = std::max(20.0f, track * track / (track + h.max_scroll));
            push_rect(DrawOp::Kind::Fill, px + panel_w - 6, py + 4 + (track - thumb) * (h.scroll / h.max_scroll), 3, thumb, with_alpha(fg, 0.3), 1, 2);
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
        close_completion(false);
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
        const bool current = ed.seen_stamp == node->tree_stamp();
        ed.synced = ed.buffer.text();
        mutable_node(*node).set_attribute("value", ed.synced);
        if (current) ed.seen_stamp = node->tree_stamp();
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
        if (ed.box_mode) {
            const CodePos at = code_pos_at(ed, px, py);
            const float fx = (px - ed.text_x + ed.scroll_x) / std::max(1.0f, ed.char_w);
            const std::size_t column = fx <= 0.0f ? 0 : static_cast<std::size_t>(std::lround(fx));
            ed.buffer.box_select(ed.box_line, ed.box_column, at.line, column, ed.tab_size);
            ed.reveal = true;
            return;
        }
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

    void read_find_attributes(const Node& element, CodeEditor& ed) {
        CodeFindOptions options;
        options.match_case = element.attribute_or("find-case", "false") == "true";
        options.whole_word = element.attribute_or("find-word", "false") == "true";
        options.regex = element.attribute_or("find-regex", "false") == "true";
        const std::string query = element.attribute_or("find", "");
        const std::string key = query + '\x1f' + (options.match_case ? '1' : '0') + (options.whole_word ? '1' : '0') + (options.regex ? '1' : '0');
        if (key != ed.find_key) {
            ed.find_key = key;
            ed.find_query = query;
            ed.find_options = options;
            ed.find_revision = static_cast<std::size_t>(-1);
            ed.find_new_query = true;
        }
        ed.find_replace = element.attribute_or("replace", "");
        const std::string command = element.attribute_or("find-command", "");
        if (!command.empty() && command != ed.find_request) {
            ed.find_request = command;
            ed.find_pending = command.substr(0, command.find('#'));
        }
    }

    void refresh_find(CodeEditor& ed) {
        if (ed.find_revision == ed.buffer.revision()) return;
        ed.find_revision = ed.buffer.revision();
        ed.matches = find_code_matches(ed.buffer.lines(), ed.find_query, ed.find_options, &ed.find_bad);
        const auto [sel_a, sel_b] = ed.buffer.selection();
        ed.find_current = static_cast<std::size_t>(-1);
        if (ed.matches.empty()) {
            ed.find_new_query = false;
            return;
        }
        if (ed.find_new_query) {
            ed.find_new_query = false;
            ed.find_current = code_match_at_or_after(ed.matches, sel_a);
            const CodeMatch& m = ed.matches[ed.find_current];
            ed.buffer.select(m.begin, m.end);
            ed.reveal = true;
            poke();
            return;
        }
        for (std::size_t i = 0; i < ed.matches.size(); ++i) {
            if (ed.matches[i].begin == sel_a && ed.matches[i].end == sel_b) ed.find_current = i;
        }
    }

    bool selection_is_match(const CodeEditor& ed, std::size_t index) const {
        if (index >= ed.matches.size()) return false;
        const auto [a, b] = ed.buffer.selection();
        return !ed.buffer.has_extra_cursors() && ed.matches[index].begin == a && ed.matches[index].end == b;
    }

    void go_to_match(CodeEditor& ed, std::size_t index) {
        if (index >= ed.matches.size()) return;
        ed.find_current = index;
        ed.buffer.clear_extra_cursors();
        ed.buffer.select(ed.matches[index].begin, ed.matches[index].end);
        ed.reveal = true;
    }

    void run_find_requests() {
        for (const Node* node : m_code_laid) {
            if (!node || !document().is_attached(*node)) continue;
            const auto found = m_code.find(node);
            if (found == m_code.end()) continue;
            CodeEditor& ed = found->second;
            const std::string pending = std::move(ed.find_pending);
            ed.find_pending.clear();
            if (!pending.empty()) run_find_command(*node, ed, pending);
            const std::size_t current = ed.find_current < ed.matches.size() ? ed.find_current : static_cast<std::size_t>(-1);
            if (current == ed.reported_current && ed.matches.size() == ed.reported_count && ed.find_bad == ed.reported_bad) continue;
            ed.reported_current = current;
            ed.reported_count = ed.matches.size();
            ed.reported_bad = ed.find_bad;
            Event e;
            e.type = "find";
            e.bubbles = true;
            e.x = current == static_cast<std::size_t>(-1) ? 0.0 : static_cast<double>(current + 1);
            e.y = static_cast<double>(ed.matches.size());
            e.key = ed.find_bad ? "bad-pattern" : "";
            dispatch(node, e);
        }
    }

    void run_find_command(const Node& node, CodeEditor& ed, const std::string& command) {
        refresh_find(ed);
        const bool read_only = code_read_only(node) || is_disabled(&node);
        const std::size_t n = ed.matches.size();
        const std::size_t none = static_cast<std::size_t>(-1);
        if (command == "seed") {
            const auto [a, b] = ed.buffer.selection();
            Event seed;
            seed.type = "find-seed";
            seed.bubbles = true;
            if (a.line == b.line && a != b && b.column - a.column <= 400) seed.text = ed.buffer.text_between(a, b);
            dispatch(&node, seed);
            return;
        }
        if (command == "next" || command == "prev") {
            if (n == 0) return;
            const auto [a, b] = ed.buffer.selection();
            std::size_t index;
            if (selection_is_match(ed, ed.find_current)) index = command == "next" ? (ed.find_current + 1) % n : (ed.find_current + n - 1) % n;
            else index = command == "next" ? code_match_at_or_after(ed.matches, b) : code_match_before(ed.matches, a);
            go_to_match(ed, index);
        } else if (command == "select-all") {
            if (n == 0) return;
            go_to_match(ed, 0);
            for (std::size_t i = 1; i < n; ++i) ed.buffer.add_cursor(ed.matches[i].begin, ed.matches[i].end);
            ed.find_current = 0;
        } else if (command == "replace") {
            if (n == 0 || read_only) return;
            if (!selection_is_match(ed, ed.find_current)) {
                const auto [a, b] = ed.buffer.selection();
                (void)b;
                go_to_match(ed, code_match_at_or_after(ed.matches, a));
                return;
            }
            const std::string text = code_replacement_for(ed.buffer.lines(), ed.matches[ed.find_current], ed.find_query, ed.find_replace, ed.find_options);
            ed.buffer.insert(text);
            const CodePos after = ed.buffer.caret();
            ed.find_revision = none;
            refresh_find(ed);
            code_changed(&node);
            if (!ed.matches.empty()) go_to_match(ed, code_match_at_or_after(ed.matches, after));
        } else if (command == "replace-all") {
            if (n == 0 || read_only) return;
            const std::vector<std::string>& lines = ed.buffer.lines();
            std::string joined;
            CodePos at = ed.matches.front().begin;
            for (const CodeMatch& m : ed.matches) {
                joined += ed.buffer.text_between(at, m.begin);
                joined += code_replacement_for(lines, m, ed.find_query, ed.find_replace, ed.find_options);
                at = m.end;
            }
            const CodePos first = ed.matches.front().begin;
            ed.buffer.clear_extra_cursors();
            ed.buffer.select(first, ed.matches.back().end);
            ed.buffer.insert(joined);
            ed.buffer.set_caret(first);
            ed.find_revision = none;
            refresh_find(ed);
            ed.find_current = none;
            code_changed(&node);
            Event done;
            done.type = "replace";
            done.bubbles = true;
            done.x = static_cast<double>(n);
            dispatch(&node, done);
        }
        ed.reveal = true;
        poke();
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
        bool skip_column = false;
        {
            const Chord pressed = chord_of(fizmo::input::key_name(key), modifiers_of(mods));
            if (!pressed.empty()) {
                const Keymap overrides = parse_keymap(node->attribute_or("keys", ""));
                const std::optional<std::string> action = code_action_for(overrides, pressed);
                const KeyAction* bound = action ? find_code_editor_action(*action) : nullptr;
                if (bound && !bound->mouse && !bound->defaults.empty()) {
                    const Chord& d = bound->defaults.front();
                    if (d != pressed) {
                        key = fizmo::input::key_from_name(d.key);
                        Modifiers next = Modifiers::None;
                        if (d.ctrl) next |= Modifiers::Control;
                        if (d.shift) next |= Modifiers::Shift;
                        if (d.alt) next |= Modifiers::Alt;
                        mods = next;
                    }
                } else if (!bound && is_code_default_chord(pressed)) {
                    if (!(pressed.shift && !pressed.ctrl && !pressed.alt && (pressed.key == "Up" || pressed.key == "Down"))) return;
                    skip_column = true;
                }
            }
        }
        CodeEditor& ed = m_code[node];
        CodeBuffer& buf = ed.buffer;
        const bool shift = fizmo::input::has(mods, Modifiers::Shift);
        const bool ctrl = fizmo::input::has(mods, Modifiers::Control) || fizmo::input::has(mods, Modifiers::Meta);
        const bool ro = code_read_only(*node);
        const bool spaces = node->attribute_or("indent", "spaces") != "tabs";
        const std::size_t page = static_cast<std::size_t>(std::max(1.0f, ed.view_h / std::max(1.0f, ed.line_h) - 1.0f));
        bool edited = false;
        const bool vertical = key == Key::Up || key == Key::Down;
        if (vertical && shift && !ctrl && !skip_column && (ed.column_mode || (!buf.any_selection() && !buf.has_extra_cursors()))) {
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
            case Key::Backspace:
                if (!ro) {
                    if (!ctrl && node && node->attribute_or("tab-backspace", "false") != "false") buf.backspace_to_tab_stop(ed.tab_size);
                    else buf.backspace(ctrl);
                    edited = true;
                }
                break;
            case Key::Delete: if (!ro) { buf.delete_forward(ctrl); edited = true; } break;
            case Key::Enter:
            case Key::NumpadEnter:
                if (!ro) {
                    const bool close = node && node->attribute_or("close-blocks", "false") != "false";
                    const CodeLanguage lang = code_language_from_name(node ? node->attribute_or("language", "plain") : std::string("plain"));
                    buf.new_line(ed.tab_size, spaces, lang, close);
                    edited = true;
                }
                break;
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
                        if (buf.any_selection()) put_clipboard(m_native, buf.selected_texts());
                    } else {
                        put_clipboard(m_native, buf.has_selection() ? buf.selected_text() : buf.line(buf.caret().line) + "\n");
                    }
                }
                break;
            case Key::X:
                if (ctrl && m_native && !ro) {
                    if (buf.has_extra_cursors()) {
                        if (buf.any_selection()) {
                            put_clipboard(m_native, buf.selected_texts());
                            buf.delete_selection();
                            edited = true;
                        }
                        break;
                    }
                    if (!buf.has_selection()) buf.select_line(buf.caret().line);
                    put_clipboard(m_native, buf.selected_text());
                    buf.delete_selection();
                    edited = true;
                }
                break;
            case Key::V:
                if (ctrl && m_native && !ro) {
                    const std::string text = read_clipboard(m_native);
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
            case Key::Slash:
                if (ctrl && !ro) {
                    const CodeLanguage lang = code_language_from_name(node ? node->attribute_or("language", "plain") : std::string("plain"));
                    std::string sample;
                    for (std::size_t l = 0; l < buf.line_count() && l < 400; ++l) sample += buf.line(l) + "\n";
                    const std::string marker = node && node->has_attribute("line-comment") ? node->attribute_or("line-comment", "") : code_line_comment(lang, sample);
                    if (!marker.empty()) {
                        buf.toggle_comment(marker);
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
    SelectMenu m_menu;
    ContextMenu m_context;
    std::vector<TermView> m_term_views;
    std::vector<TextRun> m_runs;
    std::vector<RunRef> m_run_refs;
    std::vector<const Node*> m_open_modals;
    std::vector<const Node*> m_floats;
    std::uint64_t m_float_scan_stamp = 0;
    std::uint64_t m_float_scan_revision = static_cast<std::uint64_t>(-1);
    const Node* m_floating_node = nullptr;
    std::uint64_t m_modal_scan_stamp = 0;
    std::uint64_t m_modal_scan_revision = static_cast<std::uint64_t>(-1);
    CodeCompletion m_complete;
    fizmo::graphics::Renderer* m_renderer = nullptr;
    TextPoint m_sel_anchor;
    TextPoint m_sel_focus;
    bool m_sel_pending = false;
    bool m_sel_dragged = false;
    float m_sel_press_x = 0;
    float m_sel_press_y = 0;
    std::map<const Node*, std::size_t> m_doc_order;
    std::uint64_t m_doc_order_rev = ~std::uint64_t(0);
    const Node* m_term_select = nullptr;
    TermPoint m_term_anchor;
    const Node* m_term_mouse = nullptr;
    int m_term_button = 0;
    int m_term_last_col = -1;
    int m_term_last_row = -1;
    bool m_term_swallow = false;
    bool m_swallow_text = false;
    std::map<const Node*, std::array<std::uint8_t, 9>> m_term_colors;
    std::map<const Node*, CanvasTexture> m_canvas_textures;
    CodeHover m_code_hover;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    std::shared_ptr<fizmo::images::BitmapImage> m_color_wheel_image;
    std::shared_ptr<fizmo::graphics::Texture> m_color_wheel;
    std::vector<ElementBox> m_boxes;
    std::unordered_map<const Node*, LayoutEntry> m_layout_cache;
    std::vector<const DrawOp*> m_draw_list;
    std::vector<DrawLayer> m_draw_layers;
    std::vector<unsigned char> m_redundant_clip;
    bool m_batching = std::getenv("GROVE_BATCHING") == nullptr || std::string(std::getenv("GROVE_BATCHING")) != "0";
    std::unordered_map<const Node*, ChildCache> m_child_cache;
    bool m_force_full = false;
    bool m_partial_runs = false;
    const Node* m_pending_select = nullptr;
    bool m_layout_caching = std::getenv("GROVE_LAYOUT_CACHE") == nullptr || std::string(std::getenv("GROVE_LAYOUT_CACHE")) != "0";
    int m_capturing = 0;
    std::uint64_t m_frame_index = 0;
    std::vector<const Node*> m_hover;
    std::vector<const Node*> m_active;
    const Node* m_pressed = nullptr;
    const Node* m_focused = nullptr;
    const Node* m_focus = nullptr;
    std::string m_edit;
    std::string m_focus_start;
    bool m_invalid = false;
    bool m_edit_all = false;
    std::size_t m_edit_caret = 0;
    float m_edit_scroll = 0.0f;
    MathInputLayout m_math_layout;
    std::string m_math_layout_text;
    float m_edit_click_x = -1.0f;
    float m_edit_click_y = -1.0f;
    bool m_edit_click_extend = false;
    bool m_edit_dragging = false;
    std::size_t m_edit_anchor = std::string::npos;
    std::vector<std::pair<std::string, std::size_t>> m_edit_undo;
    std::vector<std::pair<std::string, std::size_t>> m_edit_redo;
    int m_edit_undo_kind = 0;
    std::chrono::steady_clock::time_point m_edit_undo_time;
    const Node* m_edit_view = nullptr;
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
    double m_last_layout_ms = 0.0;
    double m_last_draw_ms = 0.0;
    std::size_t m_last_ops = 0;
    float m_mouse_x = 0.0f;
    float m_mouse_y = 0.0f;
    unsigned m_mods = 0;
    std::string m_cursor;
    std::unordered_map<const Node*, double> m_opacity;
    std::chrono::steady_clock::time_point m_last_activity = std::chrono::steady_clock::now();
    std::unordered_map<std::string, float> m_measure;
    std::vector<Rect> m_clips;
    std::vector<Scroller> m_scrollers;
    BarDrag m_bar_drag;
    BarDrag m_bar_hot;
    std::unordered_map<const Node*, float> m_elem_scroll;
    std::unordered_map<const Node*, float> m_elem_scroll_x;
    std::unordered_map<const Node*, std::string> m_revealed;
    const Node* m_reveal_node = nullptr;
    float m_reveal_y = 0.0f;
    std::string m_reveal_value;
    std::unordered_map<const Node*, float> m_elem_scroll_end;
    std::vector<const Node*> m_code_laid;
    int m_wide = 0;
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
    unsigned min_width = 0;
    unsigned min_height = 0;
    bool auto_height = false;
    bool fitted = false;
    bool close_requested = false;
    bool blocked = false;
    std::string size_attr;
    int x = 0;
    int y = 0;
    bool placed = false;
    double paint_ms = 0.0;
    double present_ms = 0.0;
    std::uint64_t frames = 0;
    std::deque<std::chrono::steady_clock::time_point> frame_times;
    ~WindowHost() { forget_clipboard_window(window.get()); }
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
        host.computed_number = [this](const Node& n, const std::string& property, std::size_t index, double& out) {
            WindowHost* h = host_containing(n);
            return h && h->view->computed_number(n, property, index, out);
        };
        host.computed_color = [this](const Node& n, const std::string& property) {
            WindowHost* h = host_containing(n);
            return h ? h->view->computed_color(n, property) : std::string();
        };
        host.focus = [this](Node* n) {
            WindowHost* h = n ? host_containing(*n) : host_for(&m_main);
            return h && h->view->focus_from_script(n);
        };
        host.window_info = [this](const Node& n, WindowInfo& info) {
            WindowHost* wh = host_for(&n);
            if (!wh) wh = host_containing(n);
            if (!wh || !wh->window || !wh->window->is_open()) return false;
            info.width = wh->window->width();
            info.height = wh->window->height();
            wh->window->position(info.x, info.y);
            info.dpi_scale = wh->window->dpi_scale();
            const std::vector<fizmo::windows::MonitorInfo> all = fizmo::windows::monitors();
            const int index = wh->window->monitor_index();
            fizmo::windows::MonitorInfo monitor = index >= 0 && static_cast<std::size_t>(index) < all.size() ? all[static_cast<std::size_t>(index)] : fizmo::windows::primary_monitor();
            info.monitor = monitor.name;
            info.monitor_width = monitor.bounds.width;
            info.monitor_height = monitor.bounds.height;
            info.refresh_rate = monitor.current.refresh_rate;
            info.layout_ms = wh->view->last_layout_ms();
            info.draw_ms = wh->view->last_draw_ms();
            info.present_ms = wh->present_ms;
            info.frame_ms = wh->paint_ms + wh->present_ms;
            info.draw_ops = wh->view->last_ops();
            info.frames = wh->frames;
            const auto now = std::chrono::steady_clock::now();
            while (!wh->frame_times.empty() && now - wh->frame_times.front() > std::chrono::seconds(1)) wh->frame_times.pop_front();
            info.frames_per_second = static_cast<double>(wh->frame_times.size());
            info.renderer = wh->renderer ? wh->renderer->backend_name() : "";
            return true;
        };
        host.native_window = [this](const Node& n, std::string_view what) -> void* {
            WindowHost* wh = host_for(&n);
            if (!wh) wh = host_containing(n);
            if (!wh) return nullptr;
            if (what == "window") return wh->window.get();
            if (what == "renderer") return wh->renderer.get();
            return nullptr;
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
            return put_clipboard(wh->window.get(), text);
        };
        host.clipboard = [this]() {
            WindowHost* wh = host_for(&m_main);
            if (!wh || !wh->window || !wh->window->is_open()) return std::string();
            return read_clipboard(wh->window.get());
        };
        host.open_menu = [this](Node& owner, const std::vector<MenuItem>& items, double x, double y) {
            WindowHost* wh = host_containing(owner);
            return wh && wh->view->open_context_menu(owner, items, x, y);
        };
        host.close_menu = [this]() {
            bool closed = false;
            for (const auto& wh : m_hosts) closed = wh->view->close_context_menu(true) || closed;
            return closed;
        };
        host.selected_text = [this]() {
            for (const auto& wh : m_hosts) {
                const std::string t = wh->view->public_selected_text();
                if (!t.empty()) return t;
            }
            return std::string();
        };
        host.select_text = [this](Node& node) {
            WindowHost* wh = host_containing(node);
            return wh && wh->view->public_select(node);
        };
        host.clear_selection = [this]() {
            for (const auto& wh : m_hosts) wh->view->public_clear_selection();
        };
    }

    ~WindowManager() {
        while (!m_hosts.empty()) destroy_host(m_hosts.back().get());
        DocumentHost& host = m_doc.host();
        host.computed_style = nullptr;
        host.computed_number = nullptr;
        host.computed_color = nullptr;
        host.focus = nullptr;
        host.window_size = nullptr;
        host.window_info = nullptr;
        host.native_window = nullptr;
        host.resize_window = nullptr;
        host.move_window = nullptr;
        host.focus_window = nullptr;
        host.file_dialog = nullptr;
        host.set_clipboard = nullptr;
        host.clipboard = nullptr;
        host.open_menu = nullptr;
        host.close_menu = nullptr;
        host.selected_text = nullptr;
        host.select_text = nullptr;
        host.clear_selection = nullptr;
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
            bool quiet = true;
            for (const auto& h : m_hosts) quiet = quiet && h->view->idle();
            const Clock::time_point before = Clock::now();
            if (!quiet || m_force_render || m_doc.revision() != m_painted_revision || m_doc.root().tree_stamp() != m_painted_stamp || m_hosts.size() != m_painted_hosts || before - m_painted_at >= std::chrono::milliseconds(500)) {
                render();
                frame_stats_log().report();
                m_painted_revision = m_doc.revision();
                m_painted_stamp = m_doc.root().tree_stamp();
                m_painted_hosts = m_hosts.size();
                m_painted_at = before;
            } else {
                ++frame_stats_log().skipped;
            }
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
        const double min_w = std::max(0.0, style.length("min-size", 0).value_or(0.0));
        const double min_h = std::max(0.0, style.length("min-size", 1).value_or(min_w));
        host->min_width = static_cast<unsigned>(std::lround(min_w));
        host->min_height = static_cast<unsigned>(std::lround(min_h));
        host->width = std::max({ width, 60u, host->min_width });
        host->height = std::max({ height, 40u, host->min_height });
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
        else if (host->min_width > 0 || host->min_height > 0) host->window->set_min_size(host->min_width, host->min_height);
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
        for (const WindowEventType type : { WindowEventType::MouseClick, WindowEventType::MouseRelease, WindowEventType::MouseMove, WindowEventType::MouseDoubleClick, WindowEventType::MouseScroll, WindowEventType::KeyPress, WindowEventType::KeyRelease, WindowEventType::TextInput, WindowEventType::FilesDropped }) {
            host.window->add_event_listener(type, [raw](const fizmo::windows::WindowEvent& e) { raw->view->on_event(e); });
        }
        host.window->set_drop_enabled(true);
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
        for (const WindowEventType type : { WindowEventType::WindowResize, WindowEventType::WindowExpose, WindowEventType::WindowMaximize, WindowEventType::WindowRestore }) {
            host.window->add_event_listener(type, [this, raw](const fizmo::windows::WindowEvent&) { render_now(raw); });
        }
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
        forget_clipboard_window(host->window.get());
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

    void render_now(WindowHost* host) {
        if (m_rendering || !host_alive(host) || !host->window || !host->window->is_open() || host->window->width() == 0 || host->window->height() == 0) return;
        m_rendering = true;
        fizmo::graphics::Renderer& r = *host->renderer;
        const auto started = std::chrono::steady_clock::now();
        r.begin_frame();
        r.clear(host->background);
        const std::uint64_t revision = m_doc.revision();
        host->view->paint(r);
        if (m_doc.revision() != revision) {
            r.clear(host->background);
            host->view->paint(r);
        }
        const auto painted = std::chrono::steady_clock::now();
        r.present();
        note_frame(*host, started, painted, std::chrono::steady_clock::now());
        m_rendering = false;
        m_force_render = true;
    }

    void render() {
        std::vector<WindowHost*> snapshot;
        for (const auto& h : m_hosts) snapshot.push_back(h.get());
        m_rendering = true;
        m_force_render = false;
        for (WindowHost* h : snapshot) {
            if (!host_alive(h) || !h->window || !h->window->is_open()) continue;
            fizmo::graphics::Renderer& r = *h->renderer;
            const auto started = std::chrono::steady_clock::now();
            r.begin_frame();
            r.clear(h->background);
            h->view->paint(r);
            const auto painted = std::chrono::steady_clock::now();
            r.present();
            note_frame(*h, started, painted, std::chrono::steady_clock::now());
            FrameStatsLog& stats = frame_stats_log();
            if (stats.enabled) {
                const auto presented = std::chrono::steady_clock::now();
                ++stats.frames;
                stats.present_ms += FrameStatsLog::ms(painted, presented);
                stats.worst_ms = std::max(stats.worst_ms, FrameStatsLog::ms(started, presented));
            }
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
        m_rendering = false;
    }

    static void note_frame(WindowHost& host, std::chrono::steady_clock::time_point started, std::chrono::steady_clock::time_point painted, std::chrono::steady_clock::time_point presented) {
        host.paint_ms = std::chrono::duration<double, std::milli>(painted - started).count();
        host.present_ms = std::chrono::duration<double, std::milli>(presented - painted).count();
        ++host.frames;
        host.frame_times.push_back(presented);
        while (host.frame_times.size() > 1000) host.frame_times.pop_front();
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
    bool m_rendering = false;
    bool m_force_render = false;
    Node& m_main;
    std::vector<std::unique_ptr<WindowHost>> m_hosts;
    std::uint64_t m_painted_revision = static_cast<std::uint64_t>(-1);
    std::uint64_t m_painted_stamp = 0;
    std::size_t m_painted_hosts = 0;
    std::chrono::steady_clock::time_point m_painted_at{};
    std::uint64_t m_seen_revision = ~std::uint64_t(0);
    bark::Viewport m_screen{ 1920, 1080 };
};

} // namespace

std::vector<std::string> font_families(std::string_view kind) {
    std::vector<std::string> out;
    auto add = [&out](const char* name) {
        const std::string n(name);
        if (n.rfind("Invalid", 0) != 0) out.push_back(n);
    };
    const bool all = kind.empty() || kind == "all";
#define GROVE_FONT_NAME(name, css) add(css);
    if (all || kind == "monospace") { FIZMO_MONOSPACE_FONTS(GROVE_FONT_NAME) }
    if (all || kind == "sans-serif") { FIZMO_SANS_SERIF_FONTS(GROVE_FONT_NAME) }
    if (all || kind == "serif") { FIZMO_SERIF_FONTS(GROVE_FONT_NAME) }
    if (all || kind == "display") { FIZMO_DISPLAY_FONTS(GROVE_FONT_NAME) }
    if (all || kind == "handwriting") { FIZMO_HANDWRITING_FONTS(GROVE_FONT_NAME) }
#undef GROVE_FONT_NAME
    return out;
}

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
            if (tools.cxx.empty()) {
                tools.cxx = defaults.cxx;
                tools.compiler_from_walnut = defaults.compiler_from_walnut;
            }
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
