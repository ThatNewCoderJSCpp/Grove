#include "grove/input/input.hpp"
#include "grove/input/element_table.hpp"
#include "grove/input/ion_select.hpp"
#include "grove/input/quantity.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace grove {

namespace {

struct KindName {
    const char* name;
    InputKind kind;
};

constexpr KindName k_tags[] = {
    { "input_integer", InputKind::Integer },
    { "input_float",   InputKind::Float },
    { "input_string",  InputKind::String },
    { "input_math",    InputKind::String },
    { "input_formula", InputKind::String },
    { "input_bool",    InputKind::Bool },
    { "input_radio",   InputKind::Radio },
    { "input_slider",  InputKind::Slider },
    { "input_color",   InputKind::Color },
    { "input_code",    InputKind::Code },
    { "input_file",    InputKind::File },
    { "input_select",  InputKind::Select },
    { "input_element", InputKind::Element },
    { "periodic_table", InputKind::Element },
    { "input_ion",     InputKind::Select },
    { "polyatomic_ion", InputKind::Select },
    { "input_acid",    InputKind::Select },
    { "input_base",    InputKind::Select },
    { "checkbox",      InputKind::Bool },
    { "slider",        InputKind::Slider },
    { "color",         InputKind::Color },
};

constexpr KindName k_type_names[] = {
    { "integer",  InputKind::Integer },
    { "int",      InputKind::Integer },
    { "float",    InputKind::Float },
    { "number",   InputKind::Float },
    { "string",   InputKind::String },
    { "text",     InputKind::String },
    { "bool",     InputKind::Bool },
    { "boolean",  InputKind::Bool },
    { "checkbox", InputKind::Bool },
    { "toggle",   InputKind::Bool },
    { "radio",    InputKind::Radio },
    { "slider",   InputKind::Slider },
    { "range",    InputKind::Slider },
    { "color",    InputKind::Color },
    { "code",     InputKind::Code },
    { "file",     InputKind::File },
    { "select",   InputKind::Select },
    { "dropdown", InputKind::Select },
    { "element",  InputKind::Element },
};

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

std::size_t code_points(std::string_view s) noexcept {
    std::size_t n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string truncate_code_points(std::string s, std::size_t limit) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == limit) {
                s.resize(i);
                return s;
            }
            ++n;
        }
    }
    return s;
}

std::optional<double> float_attribute(const Node& node, std::string_view name) {
    const Attribute* a = node.find_attribute(name);
    return a ? parse_float(a->value) : std::nullopt;
}

std::optional<InputKind> kind_if(const Node& node, std::initializer_list<InputKind> kinds) {
    const std::optional<InputKind> k = input_kind_of(node);
    if (!k) return std::nullopt;
    for (InputKind want : kinds) {
        if (*k == want) return k;
    }
    return std::nullopt;
}

std::string format_integer(std::int64_t value) { return std::to_string(value); }

std::string describe(const Node& node) { return "<" + node.tag() + (node.id().empty() ? "" : " id=\"" + node.id() + "\"") + ">"; }

bool allowed_attribute(InputKind kind, const std::string& name, bool generic) {
    static const std::set<std::string> specific = { "value", "min", "max", "step", "maxlength", "placeholder", "checked", "name", "type", "switch", "allow", "block", "chars", "block-chars", "pattern" };
    if (!specific.count(name)) return true;
    if (name == "type") return generic;
    switch (kind) {
        case InputKind::Integer:
        case InputKind::Float: return name == "value" || name == "min" || name == "max" || name == "step" || name == "placeholder";
        case InputKind::Slider: return name == "value" || name == "min" || name == "max" || name == "step";
        case InputKind::String: return name == "value" || name == "maxlength" || name == "placeholder" || name == "allow" || name == "block" || name == "chars" || name == "block-chars" || name == "pattern";
        case InputKind::Bool: return name == "checked" || name == "switch";
        case InputKind::Radio: return name == "name" || name == "value" || name == "checked";
        case InputKind::Color: return name == "value" || name == "format" || name == "picker" || name == "alpha";
        case InputKind::Code: return name == "value" || name == "placeholder" || name == "name";
        case InputKind::File: return name == "value" || name == "placeholder" || name == "name";
        case InputKind::Select: return name == "value" || name == "placeholder" || name == "name";
        case InputKind::Element: return name == "value" || name == "name" || name == "placeholder";
    }
    return false;
}

} // namespace

const char* to_string(InputKind kind) noexcept {
    switch (kind) {
        case InputKind::Integer: return "integer";
        case InputKind::Float: return "float";
        case InputKind::String: return "string";
        case InputKind::Bool: return "bool";
        case InputKind::Radio: return "radio";
        case InputKind::Slider: return "slider";
        case InputKind::Color: return "color";
        case InputKind::Code: return "code";
        case InputKind::File: return "file";
        case InputKind::Select: return "select";
        case InputKind::Element: return "element";
    }
    return "string";
}

const char* input_tag(InputKind kind) noexcept {
    switch (kind) {
        case InputKind::Integer: return "input_integer";
        case InputKind::Float: return "input_float";
        case InputKind::String: return "input_string";
        case InputKind::Bool: return "input_bool";
        case InputKind::Radio: return "input_radio";
        case InputKind::Slider: return "input_slider";
        case InputKind::Color: return "input_color";
        case InputKind::Code: return "input_code";
        case InputKind::File: return "input_file";
        case InputKind::Select: return "input_select";
        case InputKind::Element: return "input_element";
    }
    return "input_string";
}

std::optional<InputKind> input_kind_from_name(std::string_view name) noexcept {
    for (const KindName& k : k_type_names) {
        if (name == k.name) return k.kind;
    }
    return std::nullopt;
}

std::optional<InputKind> input_kind_of(const Node& node) noexcept {
    if (!node.is_element()) return std::nullopt;
    for (const KindName& k : k_tags) {
        if (node.tag() == k.name) return k.kind;
    }
    if (node.tag() == "input") {
        const Attribute* type = node.find_attribute("type");
        return type ? input_kind_from_name(type->value) : std::nullopt;
    }
    return std::nullopt;
}

bool is_input(const Node& node) noexcept { return input_kind_of(node).has_value(); }

bool is_text_input(InputKind kind) noexcept {
    return kind == InputKind::Integer || kind == InputKind::Float || kind == InputKind::String || kind == InputKind::Color;
}

bool is_string_input(InputKind kind) noexcept {
    return kind == InputKind::String || kind == InputKind::Code || kind == InputKind::File || kind == InputKind::Select || kind == InputKind::Element;
}

std::string code_source_text(const Node& node) {
    std::string raw = node.inner_text();
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= raw.size(); ++i) {
        if (i == raw.size() || raw[i] == '\n') {
            std::string line = raw.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    auto blank = [](const std::string& l) { return l.find_first_not_of(" \t") == std::string::npos; };
    while (!lines.empty() && blank(lines.front())) lines.erase(lines.begin());
    while (!lines.empty() && blank(lines.back())) lines.pop_back();
    std::size_t common = std::string::npos;
    for (const std::string& l : lines) {
        if (blank(l)) continue;
        common = std::min(common, l.find_first_not_of(" \t"));
    }
    if (common == std::string::npos) common = 0;
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        if (lines[i].size() >= common) out += lines[i].substr(common);
    }
    return out;
}

bool is_switch(const Node& node, const std::optional<std::string>& appearance) noexcept {
    if (input_kind_of(node) != InputKind::Bool) return false;
    if (node.has_attribute("switch")) return true;
    if (node.tag() == "input" && node.attribute_or("type", "") == "toggle") return true;
    return appearance && *appearance == "switch";
}

std::optional<std::int64_t> parse_integer(std::string_view text) noexcept {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && text[i] == '-') {
        negative = true;
        ++i;
    }
    if (i == text.size()) return std::nullopt;
    std::uint64_t magnitude = 0;
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1 : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    for (; i < text.size(); ++i) {
        if (!is_digit(text[i])) return std::nullopt;
        const std::uint64_t d = static_cast<std::uint64_t>(text[i] - '0');
        if (magnitude > (limit - d) / 10) return std::nullopt;
        magnitude = magnitude * 10 + d;
    }
    if (negative) return magnitude == limit ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(magnitude);
    return static_cast<std::int64_t>(magnitude);
}

bool partial_number(std::string_view text, bool negative_ok, bool integer) noexcept {
    if (text.size() > 40) return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '-') {
        if (!negative_ok) return false;
        ++i;
    }
    std::size_t digits = 0;
    bool point = false;
    while (i < text.size() && (is_digit(text[i]) || text[i] == '.')) {
        if (text[i] == '.') {
            if (point || integer) return false;
            point = true;
        } else {
            ++digits;
        }
        ++i;
    }
    if (i == text.size()) return true;
    if (text[i] != 'e' && text[i] != 'E') return false;
    if (digits == 0) return false;
    ++i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
    std::size_t exp_digits = 0;
    while (i < text.size() && is_digit(text[i])) {
        ++i;
        ++exp_digits;
    }
    return i == text.size() && exp_digits <= 5;
}

std::optional<std::int64_t> parse_integer_number(std::string_view text) {
    if (const auto plain = parse_integer(text)) return plain;
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && text[i] == '-') {
        negative = true;
        ++i;
    }
    std::string digits;
    long long point = -1;
    for (; i < text.size() && (is_digit(text[i]) || text[i] == '.'); ++i) {
        if (text[i] == '.') {
            if (point >= 0) return std::nullopt;
            point = static_cast<long long>(digits.size());
        } else {
            digits += text[i];
        }
    }
    if (digits.empty()) return std::nullopt;
    if (point < 0) point = static_cast<long long>(digits.size());
    long long exponent = 0;
    if (i < text.size()) {
        if (text[i] != 'e' && text[i] != 'E') return std::nullopt;
        ++i;
        bool exp_negative = false;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) exp_negative = text[i++] == '-';
        std::size_t exp_digits = 0;
        for (; i < text.size() && is_digit(text[i]); ++i) {
            if (++exp_digits > 5) return std::nullopt;
            exponent = exponent * 10 + (text[i] - '0');
        }
        if (exp_digits == 0 || i != text.size()) return std::nullopt;
        if (exp_negative) exponent = -exponent;
    }
    const long long whole = point + exponent;
    if (whole < 0) {
        if (digits.find_first_not_of('0') != std::string::npos) return std::nullopt;
        return 0;
    }
    std::string integer_part;
    for (long long k = 0; k < whole; ++k) integer_part += k < static_cast<long long>(digits.size()) ? digits[static_cast<std::size_t>(k)] : '0';
    for (std::size_t k = static_cast<std::size_t>(std::min<long long>(whole, static_cast<long long>(digits.size()))); k < digits.size(); ++k) {
        if (digits[k] != '0') return std::nullopt;
    }
    const std::size_t first = integer_part.find_first_not_of('0');
    integer_part = first == std::string::npos ? std::string("0") : integer_part.substr(first);
    if (integer_part.size() > 20) return std::nullopt;
    return parse_integer((negative ? "-" : "") + integer_part);
}

std::optional<double> parse_float(std::string_view text) noexcept {
    std::size_t i = 0;
    if (i < text.size() && text[i] == '-') ++i;
    std::size_t digits = 0;
    while (i < text.size() && is_digit(text[i])) { ++i; ++digits; }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && is_digit(text[i])) { ++i; ++digits; }
    }
    if (digits == 0) return std::nullopt;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
        std::size_t exp_digits = 0;
        while (i < text.size() && is_digit(text[i])) { ++i; ++exp_digits; }
        if (exp_digits == 0) return std::nullopt;
    }
    if (i != text.size()) return std::nullopt;
    const std::string s(text);
    const double v = std::strtod(s.c_str(), nullptr);
    if (!std::isfinite(v)) return std::nullopt;
    if (v == 0.0 && s.find_first_of("123456789") < s.find_first_of("eE")) return std::nullopt;
    return v;
}

std::string format_float(double value) {
    char buf[40];
    int first = 1;
    const double mag = std::fabs(value);
    if (std::isfinite(mag) && mag >= 1.0 && mag < 1e15) first = std::min(17, static_cast<int>(std::floor(std::log10(mag))) + 1);
    for (int precision = first; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, value);
        if (std::strtod(buf, nullptr) == value) break;
    }
    return buf;
}

std::string format_color(const bark::Rgba& c) {
    static const char* digits = "0123456789abcdef";
    std::string s = "#";
    auto put = [&](std::uint8_t v) {
        s += digits[v >> 4];
        s += digits[v & 15];
    };
    put(c.r);
    put(c.g);
    put(c.b);
    if (c.a != 255) put(c.a);
    return s;
}

namespace {

std::string trim_number(double value, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string out(buf);
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out == "-0") out = "0";
    return out;
}

void to_hsl(const bark::Rgba& c, double& h, double& s, double& l) {
    const double r = c.r / 255.0;
    const double g = c.g / 255.0;
    const double b = c.b / 255.0;
    const double mx = std::max({ r, g, b });
    const double mn = std::min({ r, g, b });
    const double d = mx - mn;
    l = (mx + mn) / 2.0;
    h = 0.0;
    s = 0.0;
    if (d <= 0.0) return;
    s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r) h = std::fmod((g - b) / d + 6.0, 6.0);
    else if (mx == g) h = (b - r) / d + 2.0;
    else h = (r - g) / d + 4.0;
    h *= 60.0;
}

std::uint8_t channel(double v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

bark::Rgba from_hsl(double h, double s, double l, std::uint8_t alpha) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = l - c / 2.0;
    double r = 0;
    double g = 0;
    double b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return bark::Rgba{ channel(r + m), channel(g + m), channel(b + m), alpha };
}

std::string lower_text(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '\t') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool color_number(const std::string& text, double scale_percent, double& out) {
    if (text.empty()) return false;
    std::string body = text;
    bool percent = false;
    if (body.back() == '%') {
        percent = true;
        body.pop_back();
    }
    if (body.size() > 3 && body.compare(body.size() - 3, 3, "deg") == 0) body.resize(body.size() - 3);
    char* end = nullptr;
    const double v = std::strtod(body.c_str(), &end);
    if (end == body.c_str() || *end != '\0') return false;
    out = percent ? v / 100.0 * scale_percent : v;
    return true;
}

} // namespace

std::string format_color(const bark::Rgba& c, std::string_view format) {
    std::string f = lower_text(format);
    if (c.a != 255 && (f == "rgb" || f == "hsl")) f += 'a';
    const std::string alpha = trim_number(c.a / 255.0, 2);
    if (f == "rgb") return "rgb(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ")";
    if (f == "rgba") return "rgba(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ", " + alpha + ")";
    if (f == "hsl" || f == "hsla") {
        double h = 0;
        double s = 0;
        double l = 0;
        to_hsl(c, h, s, l);
        const std::string body = trim_number(std::round(h), 0) + ", " + trim_number(std::round(s * 100.0), 0) + "%, " + trim_number(std::round(l * 100.0), 0) + "%";
        return f == "hsl" ? "hsl(" + body + ")" : "hsla(" + body + ", " + alpha + ")";
    }
    return format_color(c);
}

std::optional<fizmo::CharKind> char_kind_from_name(std::string_view name) noexcept {
    using K = fizmo::CharKind;
    static const std::pair<const char*, K> names[] = {
        { "digits", K::digits }, { "digit", K::digits }, { "numbers", K::numbers }, { "number", K::numbers }, { "letters", K::letters },
        { "letter", K::letters }, { "uppercase", K::uppercase }, { "upper", K::uppercase }, { "lowercase", K::lowercase }, { "lower", K::lowercase },
        { "spaces", K::spaces }, { "space", K::spaces }, { "whitespace", K::spaces }, { "newlines", K::newlines }, { "newline", K::newlines },
        { "punctuation", K::punctuation }, { "symbols", K::symbols }, { "symbol", K::symbols }, { "special", K::special }, { "emoji", K::emoji },
        { "control", K::control }, { "ascii", K::ascii }, { "hex", K::hex_digits }, { "hex-digits", K::hex_digits }, { "word", K::word },
        { "non-ascii", K::non_ascii },
    };
    for (const auto& [n, k] : names) {
        if (name == n) return k;
    }
    return std::nullopt;
}

bool input_has_charset(const Node& node) noexcept {
    return node.has_attribute("allow") || node.has_attribute("block") || node.has_attribute("chars") || node.has_attribute("block-chars");
}

fizmo::CharSet input_charset(const Node& node) {
    const bool allow_list = node.has_attribute("allow");
    fizmo::CharSet set = allow_list ? fizmo::CharSet::nothing() : fizmo::CharSet::everything();
    auto each = [&](const char* attribute, auto&& fn) {
        std::istringstream words(node.attribute_or(attribute, ""));
        std::string word;
        while (words >> word) {
            if (auto kind = char_kind_from_name(word)) fn(*kind);
        }
    };
    each("allow", [&](fizmo::CharKind k) { set.allow(k); });
    each("block", [&](fizmo::CharKind k) { set.disable(k, k == fizmo::CharKind::special ? fizmo::StringView(".") : fizmo::StringView()); });
    if (node.has_attribute("chars")) set.allow(node.attribute_or("chars", ""));
    if (node.has_attribute("block-chars")) set.disable(node.attribute_or("block-chars", ""));
    return set;
}

std::optional<fizmo::Regex> input_pattern(const Node& node) {
    const Attribute* a = node.find_attribute("pattern");
    if (!a) return std::nullopt;
    fizmo::Regex re(a->value);
    if (!re.ok()) return std::nullopt;
    return re;
}

bool input_text_valid(const Node& node, std::string_view text) {
    if (input_has_charset(node) && !input_charset(node).accepts(text)) return false;
    if (const auto re = input_pattern(node)) return re->matches(text);
    return true;
}

std::vector<CodeColor> find_code_colors(std::string_view line) {
    std::vector<CodeColor> out;
    const std::size_t value_start = line.find_first_of(":=");
    if (value_start == std::string_view::npos) return out;
    auto word_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; };
    std::size_t i = value_start + 1;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '/' && i + 1 < line.size() && (line[i + 1] == '/' || line[i + 1] == '*')) break;
        if (c == '#' && (i == 0 || !word_char(line[i - 1]))) {
            std::size_t j = i + 1;
            while (j < line.size() && std::isxdigit(static_cast<unsigned char>(line[j]))) ++j;
            const std::size_t digits = j - i - 1;
            if ((digits == 3 || digits == 4 || digits == 6 || digits == 8) && (j >= line.size() || !word_char(line[j]))) {
                if (auto color = parse_color_text(line.substr(i, j - i))) out.push_back(CodeColor{ i, j, *color, "hex" });
            }
            i = std::max(j, i + 1);
            continue;
        }
        if ((c == 'r' || c == 'h') && (i == 0 || !word_char(line[i - 1]))) {
            std::size_t j = i;
            while (j < line.size() && std::isalpha(static_cast<unsigned char>(line[j]))) ++j;
            const std::string name = lower_text(line.substr(i, j - i));
            if ((name == "rgb" || name == "rgba" || name == "hsl" || name == "hsla") && j < line.size() && line[j] == '(') {
                const std::size_t close = line.find(')', j);
                if (close != std::string_view::npos) {
                    if (auto color = parse_color_text(line.substr(i, close + 1 - i))) {
                        out.push_back(CodeColor{ i, close + 1, *color, name });
                        i = close + 1;
                        continue;
                    }
                }
            }
            i = std::max(j, i + 1);
            continue;
        }
        ++i;
    }
    return out;
}

std::optional<bark::Rgba> parse_color_text(std::string_view text) {
    const std::string t = lower_text(text);
    if (t.empty()) return std::nullopt;
    const std::size_t open = t.find('(');
    if (open == std::string::npos) {
        std::string hex = t;
        if (hex.front() != '#') hex.insert(hex.begin(), '#');
        for (std::size_t i = 1; i < hex.size(); ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(hex[i]))) return std::nullopt;
        }
        return bark::parse_hex_color(hex);
    }
    if (t.back() != ')') return std::nullopt;
    const std::string name = t.substr(0, open);
    std::string inside = t.substr(open + 1, t.size() - open - 2);
    for (char& c : inside) {
        if (c == '/') c = ',';
    }
    std::vector<std::string> parts;
    std::stringstream in(inside);
    std::string part;
    while (std::getline(in, part, ',')) parts.push_back(part);
    const bool wants_alpha = name == "rgba" || name == "hsla";
    if (name != "rgb" && name != "rgba" && name != "hsl" && name != "hsla") return std::nullopt;
    if (parts.size() != 3 && parts.size() != 4) return std::nullopt;
    if (wants_alpha && parts.size() != 4) return std::nullopt;
    double a = 1.0;
    if (parts.size() == 4 && !color_number(parts[3], 1.0, a)) return std::nullopt;
    const std::uint8_t alpha = channel(a);
    double v[3];
    if (name[0] == 'r') {
        for (int i = 0; i < 3; ++i) {
            if (!color_number(parts[static_cast<std::size_t>(i)], 255.0, v[i])) return std::nullopt;
        }
        return bark::Rgba{ channel(v[0] / 255.0), channel(v[1] / 255.0), channel(v[2] / 255.0), alpha };
    }
    if (!color_number(parts[0], 360.0, v[0]) || !color_number(parts[1], 1.0, v[1]) || !color_number(parts[2], 1.0, v[2])) return std::nullopt;
    if (parts[1].back() != '%') v[1] /= 100.0;
    if (parts[2].back() != '%') v[2] /= 100.0;
    return from_hsl(v[0], v[1], v[2], alpha);
}

ColorHsv color_to_hsv(const bark::Rgba& c) {
    const double r = c.r / 255.0;
    const double g = c.g / 255.0;
    const double b = c.b / 255.0;
    const double mx = std::max({ r, g, b });
    const double mn = std::min({ r, g, b });
    const double d = mx - mn;
    ColorHsv out;
    out.v = mx;
    out.s = mx > 0.0 ? d / mx : 0.0;
    if (d > 0.0) {
        if (mx == r) out.h = std::fmod((g - b) / d + 6.0, 6.0);
        else if (mx == g) out.h = (b - r) / d + 2.0;
        else out.h = (r - g) / d + 4.0;
        out.h *= 60.0;
    }
    return out;
}

bark::Rgba color_from_hsv(const ColorHsv& hsv, std::uint8_t alpha) {
    const double h = std::fmod(std::fmod(hsv.h, 360.0) + 360.0, 360.0);
    const double s = std::clamp(hsv.s, 0.0, 1.0);
    const double v = std::clamp(hsv.v, 0.0, 1.0);
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double r = 0;
    double g = 0;
    double b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return bark::Rgba{ channel(r + m), channel(g + m), channel(b + m), alpha };
}

double InputRange::clamp(double value) const noexcept {
    if (has_min && value < min) value = min;
    if (has_max && value > max) value = max;
    return value;
}

InputRange input_range(const Node& node) {
    InputRange r;
    const std::optional<InputKind> kind = input_kind_of(node);
    if (!kind) return r;
    if (const auto v = float_attribute(node, "min")) { r.min = *v; r.has_min = true; }
    if (const auto v = float_attribute(node, "max")) { r.max = *v; r.has_max = true; }
    if (*kind == InputKind::Slider) {
        if (!r.has_min) { r.min = 0.0; r.has_min = true; }
        if (!r.has_max) { r.max = 100.0; r.has_max = true; }
        r.step = 0.0;
    }
    if (const auto v = float_attribute(node, "step")) {
        if (*v > 0.0) r.step = *v;
    }
    if (*kind == InputKind::Integer) {
        if (r.has_min) r.min = std::ceil(r.min);
        if (r.has_max) r.max = std::floor(r.max);
        r.step = std::max(1.0, std::round(r.step));
    }
    return r;
}

std::optional<std::int64_t> input_integer(const Node& node) {
    if (!kind_if(node, { InputKind::Integer })) return std::nullopt;
    const InputRange range = input_range(node);
    std::int64_t value = 0;
    if (const Attribute* a = node.find_attribute("value")) {
        if (const auto v = parse_integer(a->value)) value = *v;
    }
    if (range.has_min && static_cast<double>(value) < range.min) value = static_cast<std::int64_t>(range.min);
    if (range.has_max && static_cast<double>(value) > range.max) value = static_cast<std::int64_t>(range.max);
    return value;
}

std::optional<double> input_float(const Node& node) {
    if (is_quantity(node)) return quantity_si(node);
    if (!kind_if(node, { InputKind::Float, InputKind::Slider })) return std::nullopt;
    const InputRange range = input_range(node);
    double value = range.has_min && range.min > 0.0 ? range.min : 0.0;
    if (const auto v = float_attribute(node, "value")) value = *v;
    return range.clamp(value);
}

std::vector<SelectOption> select_options(const Node& node) {
    std::vector<SelectOption> out;
    if (is_ion_select(node) || is_acid_base_select(node)) {
        for (const auto& child : node.children()) {
            if (!child->is_element() || child->tag() != "option") continue;
            SelectOption o;
            o.label = child->inner_text();
            o.value = child->has_attribute("value") ? child->attribute_or("value", "") : o.label;
            if (o.label.empty()) o.label = o.value;
            o.disabled = child->has_attribute("disabled");
            out.push_back(std::move(o));
        }
        for (SelectOption& o : (is_ion_select(node) ? ion_select_options(node) : acid_base_select_options(node))) out.push_back(std::move(o));
        return out;
    }
    for (const auto& child : node.children()) {
        if (!child->is_element() || child->tag() != "option") continue;
        SelectOption o;
        o.label = child->inner_text();
        while (!o.label.empty() && (o.label.back() == ' ' || o.label.back() == '\n' || o.label.back() == '\t' || o.label.back() == '\r')) o.label.pop_back();
        std::size_t lead = 0;
        while (lead < o.label.size() && (o.label[lead] == ' ' || o.label[lead] == '\n' || o.label[lead] == '\t' || o.label[lead] == '\r')) ++lead;
        o.label.erase(0, lead);
        o.value = child->has_attribute("value") ? child->attribute_or("value", "") : o.label;
        if (o.label.empty()) o.label = o.value;
        o.disabled = child->has_attribute("disabled");
        out.push_back(std::move(o));
    }
    return out;
}

std::optional<std::size_t> select_index(const Node& node) {
    const std::vector<SelectOption> options = select_options(node);
    if (options.empty()) return std::nullopt;
    if (const Attribute* a = node.find_attribute("value")) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (options[i].value == a->value) return i;
        }
        return std::nullopt;
    }
    if (node.has_attribute("placeholder")) return std::nullopt;
    return 0;
}

bool set_select_options(Document& document, Node& node, const std::vector<SelectOption>& options) {
    if (!kind_if(node, { InputKind::Select })) return false;
    while (!node.children().empty()) {
        Node* last = node.children().back().get();
        if (!document.remove(*last)) {
            node.clear_children();
            break;
        }
    }
    for (const SelectOption& o : options) {
        Node* option = document.create_element("option");
        option->set_attribute("value", o.value);
        if (o.disabled) option->set_attribute("disabled", "");
        option->set_inner_text(o.label.empty() ? o.value : o.label);
        document.append_child(node, *option);
    }
    document.touch();
    return true;
}

std::optional<std::string> input_string(const Node& node) {
    if (is_quantity(node)) return quantity_text(node);
    const std::optional<InputKind> kind = kind_if(node, { InputKind::String, InputKind::Code, InputKind::File, InputKind::Select, InputKind::Element });
    if (!kind) return std::nullopt;
    if (*kind == InputKind::Element) {
        if (element_has_charges(node)) {
            std::string out;
            for (const auto& ion : element_ion_selection(node)) {
                if (!out.empty()) out += ' ';
                out += element_ion_token(ion.first, ion.second);
            }
            return out;
        }
        return element_selection_text(element_selection(node));
    }
    if (*kind == InputKind::Code && !node.has_attribute("value")) return code_source_text(node);
    if (*kind == InputKind::Select && !node.has_attribute("value")) {
        const std::vector<SelectOption> options = select_options(node);
        return options.empty() || node.has_attribute("placeholder") ? std::string() : options.front().value;
    }
    return node.attribute_or("value", "");
}

std::optional<bool> input_bool(const Node& node) {
    if (!kind_if(node, { InputKind::Bool, InputKind::Radio })) return std::nullopt;
    return node.has_attribute("checked");
}

std::optional<bark::Rgba> input_color(const Node& node) {
    if (!kind_if(node, { InputKind::Color })) return std::nullopt;
    if (const Attribute* a = node.find_attribute("value")) {
        if (const auto c = parse_color_text(a->value)) return c;
    }
    return bark::Rgba{ 0, 0, 0, 255 };
}

std::optional<std::string> radio_choice(const Document& document, std::string_view group) {
    std::optional<std::string> out;
    document.root().walk([&](const Node& n) {
        if (out || input_kind_of(n) != InputKind::Radio) return;
        if (n.attribute_or("name", "") == group && n.has_attribute("checked")) out = n.attribute_or("value", "");
    });
    return out;
}

bool set_input_integer(Document& document, Node& node, std::int64_t value) {
    if (!kind_if(node, { InputKind::Integer })) return false;
    const InputRange range = input_range(node);
    if (range.has_min && static_cast<double>(value) < range.min) value = static_cast<std::int64_t>(range.min);
    if (range.has_max && static_cast<double>(value) > range.max) value = static_cast<std::int64_t>(range.max);
    node.set_attribute("value", format_integer(value));
    document.touch();
    return true;
}

bool set_input_float(Document& document, Node& node, double value) {
    if (is_quantity(node)) return set_quantity_si(document, node, value);
    if (!kind_if(node, { InputKind::Float, InputKind::Slider })) return false;
    if (!std::isfinite(value)) return false;
    const InputRange range = input_range(node);
    value = range.clamp(value);
    if (range.step > 0.0 && input_kind_of(node) == InputKind::Slider) {
        const double base = range.has_min ? range.min : 0.0;
        value = range.clamp(base + std::round((value - base) / range.step) * range.step);
        const std::string step_text = format_float(range.step);
        const std::size_t dot = step_text.find('.');
        if (dot != std::string::npos && step_text.find_first_of("eE") == std::string::npos) {
            const double scale = std::pow(10.0, static_cast<double>(step_text.size() - dot - 1));
            const double rounded = std::round(value * scale) / scale;
            if (std::isfinite(rounded)) value = range.clamp(rounded);
        }
    }
    node.set_attribute("value", format_float(value));
    document.touch();
    return true;
}

bool set_input_string(Document& document, Node& node, std::string value) {
    if (is_quantity(node)) return set_quantity_text(document, node, value);
    const std::optional<InputKind> kind = kind_if(node, { InputKind::String, InputKind::Code, InputKind::File, InputKind::Select, InputKind::Element });
    if (!kind) return false;
    if (*kind == InputKind::Element) {
        std::vector<int> numbers;
        std::string word;
        for (std::size_t i = 0; i <= value.size(); ++i) {
            if (i == value.size() || value[i] == ' ' || value[i] == ',') {
                if (!word.empty()) numbers.push_back(element_number(word));
                word.clear();
            } else {
                word += value[i];
            }
        }
        numbers.erase(std::remove(numbers.begin(), numbers.end(), 0), numbers.end());
        set_element_selection(document, node, numbers);
        return true;
    }
    if (*kind != InputKind::String) {
        node.set_attribute("value", std::move(value));
        document.touch();
        return true;
    }
    if (const Attribute* a = node.find_attribute("maxlength")) {
        if (const auto limit = parse_integer(a->value)) {
            if (*limit >= 0) value = truncate_code_points(std::move(value), static_cast<std::size_t>(*limit));
        }
    }
    node.set_attribute("value", std::move(value));
    document.touch();
    return true;
}

bool set_input_bool(Document& document, Node& node, bool value) {
    const std::optional<InputKind> kind = kind_if(node, { InputKind::Bool, InputKind::Radio });
    if (!kind) return false;
    if (*kind == InputKind::Radio && value) {
        const std::string group = node.attribute_or("name", "");
        document.root().walk([&](Node& other) {
            if (&other != &node && input_kind_of(other) == InputKind::Radio && other.attribute_or("name", "") == group) other.remove_attribute("checked");
        });
    }
    if (value) node.set_attribute("checked", "");
    else node.remove_attribute("checked");
    document.touch();
    return true;
}

bool set_input_color(Document& document, Node& node, const bark::Rgba& color) {
    if (!kind_if(node, { InputKind::Color })) return false;
    node.set_attribute("value", format_color(color));
    document.touch();
    return true;
}

bool set_radio_choice(Document& document, std::string_view group, std::string_view value) {
    Node* target = nullptr;
    document.root().walk([&](Node& n) {
        if (!target && input_kind_of(n) == InputKind::Radio && n.attribute_or("name", "") == group && n.attribute_or("value", "") == value) target = &n;
    });
    return target && set_input_bool(document, *target, true);
}

bool step_input(Document& document, Node& node, int steps) {
    const std::optional<InputKind> kind = input_kind_of(node);
    if (!kind) return false;
    const InputRange range = input_range(node);
    if (*kind == InputKind::Integer) {
        const std::int64_t v = *input_integer(node);
        const double next = static_cast<double>(v) + static_cast<double>(steps) * range.step;
        const double bounded = std::max(-9.2e18, std::min(9.2e18, next));
        return set_input_integer(document, node, static_cast<std::int64_t>(bounded));
    }
    if (*kind == InputKind::Float || *kind == InputKind::Slider) {
        double step = range.step;
        if (step <= 0.0) step = (range.max - range.min) / 100.0;
        if (step <= 0.0) step = 1.0;
        return set_input_float(document, node, *input_float(node) + steps * step);
    }
    return false;
}

std::string input_text(const Node& node) {
    const std::optional<InputKind> kind = input_kind_of(node);
    if (!kind) return {};
    switch (*kind) {
        case InputKind::Integer: return format_integer(*input_integer(node));
        case InputKind::Float:
        case InputKind::Slider: return format_float(*input_float(node));
        case InputKind::String:
        case InputKind::Code:
        case InputKind::File:
        case InputKind::Select:
        case InputKind::Element: return *input_string(node);
        case InputKind::Color: return format_color(*input_color(node), node.attribute_or("format", "hex"));
        case InputKind::Bool:
        case InputKind::Radio: return *input_bool(node) ? "true" : "false";
    }
    return {};
}

bool input_accepts(const Node& node, std::string_view current, std::string_view ch) {
    return input_accepts(node, current, ch, std::string_view());
}

bool input_accepts(const Node& node, std::string_view current, std::string_view ch, std::string_view after) {
    const std::optional<InputKind> kind = input_kind_of(node);
    if (!kind || ch.empty()) return false;
    const char c = ch.front();
    switch (*kind) {
        case InputKind::Integer:
        case InputKind::Float: {
            if (ch.size() != 1) return false;
            if (!is_digit(c) && c != '-' && c != '+' && c != '.' && c != 'e' && c != 'E') return false;
            std::string whole(current);
            whole += c;
            whole += std::string(after);
            return partial_number(whole, input_range(node).allows_negative(), *kind == InputKind::Integer);
        }
        case InputKind::Color: {
            if (ch.size() != 1) return false;
            if (!current.empty() && current.front() == '#') return is_hex(c) && current.size() - 1 < 8;
            if (c == '#') return current.empty();
            if (current.size() >= 40) return false;
            return std::isalnum(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == ',' || c == '.' || c == '%' || c == ' ' || c == '/' || c == '#';
        }
        case InputKind::String: {
            if (ch.size() == 1 && (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)) return false;
            if (input_has_charset(node) && !input_charset(node).accepts(ch)) return false;
            if (const Attribute* a = node.find_attribute("maxlength")) {
                if (const auto limit = parse_integer(a->value)) {
                    if (*limit >= 0 && code_points(current) >= static_cast<std::size_t>(*limit)) return false;
                }
            }
            return true;
        }
        default: return false;
    }
}

bool commit_input_text(Document& document, Node& node, std::string_view text) {
    const std::optional<InputKind> kind = input_kind_of(node);
    if (!kind) return false;
    switch (*kind) {
        case InputKind::Integer: {
            const auto v = parse_integer_number(text);
            return v && set_input_integer(document, node, *v);
        }
        case InputKind::Float: {
            const auto v = parse_float(text);
            return v && set_input_float(document, node, *v);
        }
        case InputKind::String:
            if (!input_text_valid(node, text)) return false;
            return set_input_string(document, node, std::string(text));
        case InputKind::Code:
        case InputKind::File: return set_input_string(document, node, std::string(text));
        case InputKind::Color: {
            const auto c = parse_color_text(text);
            return c && set_input_color(document, node, *c);
        }
        case InputKind::Slider: {
            const auto v = parse_float(text);
            return v && set_input_float(document, node, *v);
        }
        case InputKind::Select: {
            for (const SelectOption& o : select_options(node)) {
                if (o.disabled) continue;
                if (o.value == text || o.label == text) return set_input_string(document, node, o.value);
            }
            return false;
        }
        case InputKind::Element: {
            if (text.empty()) return false;
            std::string word;
            for (std::size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == ' ' || text[i] == ',') {
                    if (!word.empty() && element_number(word) == 0) return false;
                    word.clear();
                } else {
                    word += text[i];
                }
            }
            return set_input_string(document, node, std::string(text));
        }
        default: return false;
    }
}

void check_inputs(const Document& document, Diagnostics& diagnostics) {
    const std::string& file = document.path();
    std::map<std::string, std::vector<const Node*>> checked_radios;

    document.root().walk([&](const Node& node) {
        if (!node.is_element()) return;
        if (node.parent() && is_quantity(*node.parent())) return;
        if (is_quantity(node)) {
            for (const std::string& problem : quantity_problems(node)) diagnostics.warning(file, node.span(), "<" + node.tag() + (node.id().empty() ? std::string() : " id=\"" + node.id() + "\"") + "> " + problem);
            return;
        }
        const bool generic = node.tag() == "input";
        if (generic) {
            const Attribute* type = node.find_attribute("type");
            if (!type) {
                diagnostics.error(file, node.span(), "<input> needs a type (integer, float, string, bool, radio, slider, color, code, file or select), or use <input_integer>, <input_float>, ...");
                return;
            }
            if (!input_kind_from_name(type->value)) {
                diagnostics.error(file, type->span, "unknown input type '" + type->value + "' (use integer, float, string, bool, radio, slider, color, code, file or select)");
                return;
            }
        }
        const std::optional<InputKind> kind = input_kind_of(node);
        if (!kind) return;

        for (const Attribute& a : node.attributes()) {
            if (is_ion_select(node) && (a.name == "charges" || a.name == "values" || a.name == "monatomic" || a.name == "categories")) continue;
            if (!allowed_attribute(*kind, a.name, generic)) {
                diagnostics.warning(file, a.span, "'" + a.name + "' has no meaning on a " + to_string(*kind) + " input");
            }
        }

        auto number_attr = [&](const char* name, bool integer) -> std::optional<double> {
            const Attribute* a = node.find_attribute(name);
            if (!a) return std::nullopt;
            if (integer) {
                const auto v = parse_integer(a->value);
                if (!v) {
                    diagnostics.error(file, a->span, std::string(name) + "=\"" + a->value + "\" is not an integer");
                    return std::nullopt;
                }
                return static_cast<double>(*v);
            }
            const auto v = parse_float(a->value);
            if (!v) diagnostics.error(file, a->span, std::string(name) + "=\"" + a->value + "\" is not a number");
            return v;
        };

        switch (*kind) {
            case InputKind::Select: {
                const std::vector<SelectOption> options = select_options(node);
                if (options.empty()) diagnostics.warning(file, node.span(), describe(node) + " has no <option> to pick from");
                if (const Attribute* v = node.find_attribute("value"); v && !options.empty() && !select_index(node)) {
                    diagnostics.warning(file, v->span, "value=\"" + v->value + "\" isn't one of the <option> values");
                }
                for (const auto& child : node.children()) {
                    if (child->is_element() && child->tag() != "option") diagnostics.warning(file, child->span(), "only <option> elements belong inside <input_select>");
                }
                for (const std::string& problem : ion_select_problems(node)) diagnostics.warning(file, node.span(), describe(node) + " " + problem);
                for (const std::string& problem : acid_base_select_problems(node)) diagnostics.warning(file, node.span(), describe(node) + " " + problem);
                break;
            }
            case InputKind::Integer:
            case InputKind::Float:
            case InputKind::Slider: {
                const bool integer = *kind == InputKind::Integer;
                const auto value = number_attr("value", integer);
                const auto min = number_attr("min", integer);
                const auto max = number_attr("max", integer);
                const auto step = number_attr("step", integer);
                if (min && max && *min > *max) diagnostics.error(file, node.span(), describe(node) + " has min greater than max");
                if (step && *step <= 0.0) diagnostics.error(file, node.find_attribute("step")->span, "step must be greater than 0");
                if (value && ((min && *value < *min) || (max && *value > *max))) diagnostics.warning(file, node.find_attribute("value")->span, "value is outside min..max and will be clamped");
                break;
            }
            case InputKind::String: {
                const auto limit = number_attr("maxlength", true);
                if (limit && *limit < 0) diagnostics.error(file, node.find_attribute("maxlength")->span, "maxlength can't be negative");
                if (limit && *limit >= 0 && code_points(node.attribute_or("value", "")) > static_cast<std::size_t>(*limit)) {
                    diagnostics.warning(file, node.find_attribute("value")->span, "value is longer than maxlength and will be cut");
                }
                for (const char* list : { "allow", "block" }) {
                    const Attribute* a = node.find_attribute(list);
                    if (!a) continue;
                    std::istringstream words(a->value);
                    std::string word;
                    while (words >> word) {
                        if (!char_kind_from_name(word)) diagnostics.warning(file, a->span, "'" + word + "' isn't a kind of character (use letters, digits, numbers, spaces, special, punctuation, symbols, uppercase, lowercase, emoji, newlines, ascii or hex); put single characters in chars=\"...\"");
                    }
                }
                if (const Attribute* a = node.find_attribute("pattern")) {
                    const fizmo::Regex re(a->value);
                    if (!re.ok()) diagnostics.error(file, a->span, "pattern=\"" + a->value + "\" doesn't work: " + re.error() + " (at character " + std::to_string(re.error_position() + 1) + ")");
                }
                if (const Attribute* v = node.find_attribute("value")) {
                    if (input_has_charset(node) && !input_charset(node).accepts(v->value)) diagnostics.warning(file, v->span, "value has characters that allow/block don't let in");
                    else if (node.has_attribute("pattern") && input_pattern(node) && !input_pattern(node)->matches(v->value)) diagnostics.warning(file, v->span, "value doesn't fit the pattern");
                }
                break;
            }
            case InputKind::Color: {
                if (const Attribute* a = node.find_attribute("value")) {
                    if (!parse_color_text(a->value)) {
                        diagnostics.error(file, a->span, "value=\"" + a->value + "\" is not a color like #rrggbb, rgb(...) or hsl(...)");
                    }
                }
                break;
            }
            case InputKind::Radio: {
                const std::string name = node.attribute_or("name", "");
                if (name.empty()) diagnostics.error(file, node.span(), describe(node) + " needs a name=\"...\" for its group");
                if (!node.has_attribute("value")) diagnostics.error(file, node.span(), describe(node) + " needs a value=\"...\"");
                if (!name.empty() && node.has_attribute("checked")) checked_radios[name].push_back(&node);
                break;
            }
            case InputKind::File: {
                const std::string mode = node.attribute_or("mode", "open");
                if (mode != "open" && mode != "save" && mode != "folder") {
                    diagnostics.error(file, node.find_attribute("mode")->span, "mode=\"" + mode + "\" should be open, save or folder");
                }
                break;
            }
            case InputKind::Element: {
                for (const std::string& problem : element_filter_problems(node)) diagnostics.warning(file, node.span(), describe(node) + " " + problem);
                break;
            }
            case InputKind::Bool:
            case InputKind::Code: break;
        }
    });

    for (const auto& [group, nodes] : checked_radios) {
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            diagnostics.error(file, nodes[i]->span(), "radio group '" + group + "' already has a checked option");
        }
    }
}

} // namespace grove
