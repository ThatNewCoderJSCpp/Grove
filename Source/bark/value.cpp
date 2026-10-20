#include "grove/bark/value.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <sstream>

namespace grove {
namespace bark {

namespace {

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct NamedColor {
    const char* name;
    Rgba color;
};

constexpr NamedColor k_named[] = {
    { "transparent", { 0, 0, 0, 0 } },
    { "black",   { 0, 0, 0, 255 } },
    { "white",   { 255, 255, 255, 255 } },
    { "red",     { 255, 0, 0, 255 } },
    { "green",   { 0, 128, 0, 255 } },
    { "lime",    { 0, 255, 0, 255 } },
    { "blue",    { 0, 0, 255, 255 } },
    { "yellow",  { 255, 255, 0, 255 } },
    { "cyan",    { 0, 255, 255, 255 } },
    { "magenta", { 255, 0, 255, 255 } },
    { "orange",  { 255, 165, 0, 255 } },
    { "purple",  { 128, 0, 128, 255 } },
    { "pink",    { 255, 192, 203, 255 } },
    { "brown",   { 165, 42, 42, 255 } },
    { "gray",    { 128, 128, 128, 255 } },
    { "grey",    { 128, 128, 128, 255 } },
    { "silver",  { 192, 192, 192, 255 } },
    { "navy",    { 0, 0, 128, 255 } },
    { "teal",    { 0, 128, 128, 255 } },
    { "maroon",  { 128, 0, 0, 255 } },
    { "olive",   { 128, 128, 0, 255 } },
    { "walnut",  { 119, 63, 26, 255 } },
};

std::string format_number(double n) {
    std::ostringstream os;
    os << n;
    return os.str();
}

} // namespace

std::optional<Rgba> parse_hex_color(const std::string& hex) noexcept {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    for (char c : h) {
        if (hex_digit(c) < 0) return std::nullopt;
    }
    auto pair = [&](std::size_t i) { return static_cast<std::uint8_t>(hex_digit(h[i]) * 16 + hex_digit(h[i + 1])); };
    auto single = [&](std::size_t i) { return static_cast<std::uint8_t>(hex_digit(h[i]) * 17); };
    switch (h.size()) {
        case 3: return Rgba{ single(0), single(1), single(2), 255 };
        case 4: return Rgba{ single(0), single(1), single(2), single(3) };
        case 6: return Rgba{ pair(0), pair(2), pair(4), 255 };
        case 8: return Rgba{ pair(0), pair(2), pair(4), pair(6) };
        default: return std::nullopt;
    }
}

std::optional<Rgba> named_color(const std::string& name) noexcept {
    for (const NamedColor& n : k_named) {
        if (name == n.name) return n.color;
    }
    return std::nullopt;
}

std::optional<Rgba> as_color(const ValueList& values) noexcept {
    if (values.empty()) return std::nullopt;
    const Value& v = values.front();
    if (v.is_color()) return v.color;
    if (v.is_ident()) return named_color(v.text);
    return std::nullopt;
}

bool is_viewport_unit(const std::string& unit) noexcept {
    return unit == "vwidth" || unit == "vheight" || unit == "vmin" || unit == "vmax" || unit == "vdiag" || unit == "vw" || unit == "vh";
}

std::optional<double> viewport_unit_size(const std::string& unit, const Viewport& viewport) noexcept {
    if (!viewport.known()) return std::nullopt;
    const double w = viewport.width / 100.0;
    const double h = viewport.height / 100.0;
    if (unit == "vwidth" || unit == "vw") return w;
    if (unit == "vheight" || unit == "vh") return h;
    if (unit == "vmin") return std::min(w, h);
    if (unit == "vmax") return std::max(w, h);
    if (unit == "vdiag") return std::sqrt(w * w + h * h);
    return std::nullopt;
}

namespace {

struct MathReader {
    const std::vector<Value>& args;
    std::size_t end;
    double font_size;
    const Viewport& viewport;
    std::optional<double> base;
    std::size_t at = 0;

    bool is_op(const char* op) const { return at < end && args[at].is_ident(op); }

    std::optional<double> leaf() {
        if (at >= end) return std::nullopt;
        const Value& v = args[at++];
        if (v.is_ident("-")) {
            const auto inner = leaf();
            if (!inner) return std::nullopt;
            return -*inner;
        }
        if (v.is_number() && v.unit.empty()) return v.number;
        return evaluate_length(v, font_size, viewport, base);
    }

    std::optional<double> term() {
        auto left = leaf();
        while (left && (is_op("*") || is_op("/"))) {
            const bool divide = is_op("/");
            ++at;
            const auto right = leaf();
            if (!right) return std::nullopt;
            if (divide) {
                if (*right == 0.0) return std::nullopt;
                *left /= *right;
            } else {
                *left *= *right;
            }
        }
        return left;
    }

    std::optional<double> expression() {
        auto total = term();
        while (total && at < end) {
            if (is_op("+") || is_op("-")) {
                const bool minus = is_op("-");
                ++at;
                const auto right = term();
                if (!right) return std::nullopt;
                *total += minus ? -*right : *right;
            } else if (args[at].is_number() && args[at].number < 0.0) {
                const auto right = term();
                if (!right) return std::nullopt;
                *total += *right;
            } else {
                return std::nullopt;
            }
        }
        return total;
    }
};

std::optional<double> evaluate_range(const std::vector<Value>& args, std::size_t from, std::size_t to, double font_size, const Viewport& viewport, std::optional<double> base) {
    if (from >= to) return std::nullopt;
    MathReader reader{ args, to, font_size, viewport, base, from };
    const auto value = reader.expression();
    if (!value || reader.at != to) return std::nullopt;
    return value;
}

} // namespace

bool is_math_function(const Value& value) noexcept {
    return value.is_function() && (value.text == "calc" || value.text == "min" || value.text == "max" || value.text == "clamp" || value.text.empty());
}

std::optional<double> evaluate_length(const Value& v, double font_size, const Viewport& viewport, std::optional<double> percent_base) noexcept {
    if (v.is_number() && v.unit == "%") {
        if (!percent_base) return std::nullopt;
        return *percent_base * v.number / 100.0;
    }
    if (!is_math_function(v)) return as_length(v, font_size, viewport);
    std::vector<std::optional<double>> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= v.args.size(); ++i) {
        if (i < v.args.size() && !v.args[i].is_ident(",")) continue;
        parts.push_back(evaluate_range(v.args, start, i, font_size, viewport, percent_base));
        if (!parts.back()) return std::nullopt;
        start = i + 1;
    }
    if (v.text == "calc" || v.text.empty()) return parts.size() == 1 ? parts.front() : std::nullopt;
    if (v.text == "clamp") {
        if (parts.size() != 3) return std::nullopt;
        return std::max(*parts[0], std::min(*parts[1], *parts[2]));
    }
    double out = *parts.front();
    for (const auto& p : parts) out = v.text == "min" ? std::min(out, *p) : std::max(out, *p);
    return out;
}

std::optional<double> as_length(const Value& v, double font_size, const Viewport& viewport) noexcept {
    if (is_math_function(v)) return evaluate_length(v, font_size, viewport, std::nullopt);
    if (!v.is_number()) return std::nullopt;
    if (v.unit.empty()) return v.number;
    if (v.unit == "px") return v.number * viewport.scale();
    if (v.unit == "em") return v.number * font_size;
    if (v.unit == "pt") return v.number * 96.0 / 72.0 * viewport.scale();
    if (const auto per = viewport_unit_size(v.unit, viewport)) return v.number * *per;
    return std::nullopt;
}

std::optional<double> as_length(const ValueList& values, double font_size, const Viewport& viewport, std::size_t index) noexcept {
    if (index >= values.size()) return std::nullopt;
    return as_length(values[index], font_size, viewport);
}

std::optional<double> as_length(const ValueList& values, double font_size, std::size_t index) noexcept {
    return as_length(values, font_size, Viewport{}, index);
}

std::optional<std::string> as_text(const ValueList& values) {
    if (values.empty()) return std::nullopt;
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ' ';
        out += values[i].is_string() || values[i].is_ident() ? values[i].text : to_string(values[i]);
    }
    return out;
}

std::string to_string(const Value& value) {
    switch (value.kind) {
        case ValueKind::Number: return format_number(value.number) + value.unit;
        case ValueKind::String: return "\"" + value.text + "\"";
        case ValueKind::Ident: return value.text;
        case ValueKind::Color: {
            static const char* digits = "0123456789abcdef";
            std::string s = "#";
            for (std::uint8_t c : { value.color.r, value.color.g, value.color.b, value.color.a }) {
                s += digits[c >> 4];
                s += digits[c & 15];
            }
            return s;
        }
        case ValueKind::Function: {
            std::string s = value.text + "(";
            const bool math = is_math_function(value);
            for (std::size_t i = 0; i < value.args.size(); ++i) {
                if (math) {
                    if (value.args[i].is_ident(",")) {
                        s += ",";
                        continue;
                    }
                    if (i) s += ' ';
                } else if (i) {
                    s += ", ";
                }
                s += to_string(value.args[i]);
            }
            return s + ")";
        }
    }
    return {};
}

std::string to_string(const ValueList& values) {
    std::string s;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) s += ' ';
        s += to_string(values[i]);
    }
    return s;
}

bool uses_var(const ValueList& values) noexcept {
    for (const Value& v : values) {
        if (v.kind != ValueKind::Function) continue;
        if (v.text == "var" || uses_var(v.args)) return true;
    }
    return false;
}

bool substitute_vars(ValueList& values, const std::function<const ValueList*(const std::string&)>& lookup, int depth) {
    if (depth > 8) return false;
    bool all = true;
    ValueList out;
    out.reserve(values.size());
    for (Value& v : values) {
        if (v.kind == ValueKind::Function && v.text == "var") {
            const std::string name = v.args.empty() ? std::string() : v.args.front().text;
            const ValueList* found = name.empty() ? nullptr : lookup(name);
            ValueList replacement;
            if (found) replacement = *found;
            else if (v.args.size() > 1) replacement.assign(v.args.begin() + 1, v.args.end());
            else all = false;
            if (uses_var(replacement) && !substitute_vars(replacement, lookup, depth + 1)) all = false;
            out.insert(out.end(), replacement.begin(), replacement.end());
            continue;
        }
        if (v.kind == ValueKind::Function && uses_var(v.args) && !substitute_vars(v.args, lookup, depth + 1)) all = false;
        out.push_back(std::move(v));
    }
    values = std::move(out);
    return all;
}

} // namespace bark
} // namespace grove
