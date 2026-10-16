#include "grove/bark/value.hpp"

#include <cmath>
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

std::optional<double> as_length(const ValueList& values, double font_size, std::size_t index) noexcept {
    if (index >= values.size()) return std::nullopt;
    const Value& v = values[index];
    if (!v.is_number()) return std::nullopt;
    if (v.unit.empty() || v.unit == "px") return v.number;
    if (v.unit == "em") return v.number * font_size;
    if (v.unit == "pt") return v.number * 96.0 / 72.0;
    return std::nullopt;
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
            for (std::size_t i = 0; i < value.args.size(); ++i) {
                if (i) s += ", ";
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

} // namespace bark
} // namespace grove
