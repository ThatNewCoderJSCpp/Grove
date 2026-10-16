#ifndef GROVE_BARK_VALUE_HPP
#define GROVE_BARK_VALUE_HPP

#include "../common/diagnostics.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace grove {
namespace bark {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

enum class ValueKind {
    Number,
    String,
    Ident,
    Color,
    Function
};

struct Value {
    ValueKind kind = ValueKind::Ident;
    double number = 0.0;
    std::string unit;
    std::string text;
    Rgba color;
    std::vector<Value> args;
    Span span;

    static Value make_number(double n, std::string unit = {}) { Value v; v.kind = ValueKind::Number; v.number = n; v.unit = std::move(unit); return v; }
    static Value make_string(std::string s) { Value v; v.kind = ValueKind::String; v.text = std::move(s); return v; }
    static Value make_ident(std::string s) { Value v; v.kind = ValueKind::Ident; v.text = std::move(s); return v; }
    static Value make_color(Rgba c) { Value v; v.kind = ValueKind::Color; v.color = c; return v; }

    bool is_number() const noexcept { return kind == ValueKind::Number; }
    bool is_string() const noexcept { return kind == ValueKind::String; }
    bool is_ident() const noexcept { return kind == ValueKind::Ident; }
    bool is_color() const noexcept { return kind == ValueKind::Color; }
    bool is_function() const noexcept { return kind == ValueKind::Function; }
    bool is_ident(const std::string& name) const { return kind == ValueKind::Ident && text == name; }
};

using ValueList = std::vector<Value>;

std::string to_string(const Value& value);
std::string to_string(const ValueList& values);

std::optional<Rgba> parse_hex_color(const std::string& hex) noexcept;
std::optional<Rgba> named_color(const std::string& name) noexcept;
std::optional<Rgba> as_color(const ValueList& values) noexcept;
std::optional<double> as_length(const ValueList& values, double font_size = 16.0, std::size_t index = 0) noexcept;
std::optional<std::string> as_text(const ValueList& values);

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_VALUE_HPP
