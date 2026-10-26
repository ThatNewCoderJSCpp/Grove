#ifndef GROVE_INPUT_INPUT_HPP
#define GROVE_INPUT_INPUT_HPP

#include "../markup/node.hpp"
#include "../bark/value.hpp"

#include <fizmo/../../Source/String/regex.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace grove {

enum class InputKind {
    Integer,
    Float,
    String,
    Bool,
    Radio,
    Slider,
    Color,
    Code,
    File,
    Select,
    Element
};

const char* to_string(InputKind kind) noexcept;
const char* input_tag(InputKind kind) noexcept;
std::optional<InputKind> input_kind_from_name(std::string_view name) noexcept;
std::optional<InputKind> input_kind_of(const Node& node) noexcept;
bool is_input(const Node& node) noexcept;
bool is_text_input(InputKind kind) noexcept;
bool is_string_input(InputKind kind) noexcept;
std::string code_source_text(const Node& node);
bool is_switch(const Node& node, const std::optional<std::string>& appearance = std::nullopt) noexcept;

std::optional<std::int64_t> parse_integer(std::string_view text) noexcept;
std::optional<double> parse_float(std::string_view text) noexcept;
std::string format_float(double value);
std::string format_color(const bark::Rgba& color);
std::string format_color(const bark::Rgba& color, std::string_view format);
std::optional<bark::Rgba> parse_color_text(std::string_view text);

struct CodeColor {
    std::size_t begin = 0;
    std::size_t end = 0;
    bark::Rgba color;
    std::string format;
};

std::vector<CodeColor> find_code_colors(std::string_view line);

std::optional<fizmo::CharKind> char_kind_from_name(std::string_view name) noexcept;
fizmo::CharSet input_charset(const Node& node);
bool input_has_charset(const Node& node) noexcept;
std::optional<fizmo::Regex> input_pattern(const Node& node);
bool input_text_valid(const Node& node, std::string_view text);

struct ColorHsv {
    double h = 0.0;
    double s = 0.0;
    double v = 0.0;
};

ColorHsv color_to_hsv(const bark::Rgba& color);
bark::Rgba color_from_hsv(const ColorHsv& hsv, std::uint8_t alpha = 255);

struct InputRange {
    double min = 0.0;
    double max = 0.0;
    double step = 1.0;
    bool has_min = false;
    bool has_max = false;

    bool allows_negative() const noexcept { return !has_min || min < 0.0; }
    double clamp(double value) const noexcept;
};

InputRange input_range(const Node& node);

std::optional<std::int64_t> input_integer(const Node& node);
std::optional<double> input_float(const Node& node);
std::optional<std::string> input_string(const Node& node);
std::optional<bool> input_bool(const Node& node);
std::optional<bark::Rgba> input_color(const Node& node);
std::optional<std::string> radio_choice(const Document& document, std::string_view group);

struct SelectOption {
    std::string value;
    std::string label;
    bool disabled = false;
};

std::vector<SelectOption> select_options(const Node& node);
std::optional<std::size_t> select_index(const Node& node);
bool set_select_options(Document& document, Node& node, const std::vector<SelectOption>& options);

bool set_input_integer(Document& document, Node& node, std::int64_t value);
bool set_input_float(Document& document, Node& node, double value);
bool set_input_string(Document& document, Node& node, std::string value);
bool set_input_bool(Document& document, Node& node, bool value);
bool set_input_color(Document& document, Node& node, const bark::Rgba& color);
bool set_radio_choice(Document& document, std::string_view group, std::string_view value);
bool step_input(Document& document, Node& node, int steps);

std::string input_text(const Node& node);
bool input_accepts(const Node& node, std::string_view current, std::string_view utf8_char);
bool commit_input_text(Document& document, Node& node, std::string_view text);

void check_inputs(const Document& document, Diagnostics& diagnostics);

template <typename T>
struct is_input_value : std::false_type {};
template <> struct is_input_value<std::int64_t> : std::true_type {};
template <> struct is_input_value<double> : std::true_type {};
template <> struct is_input_value<bool> : std::true_type {};
template <> struct is_input_value<std::string> : std::true_type {};
template <> struct is_input_value<bark::Rgba> : std::true_type {};

template <typename T>
std::optional<T> input_value(const Node& node) {
    static_assert(is_input_value<T>::value, "input_value<T>: T must be std::int64_t, double, bool, std::string or bark::Rgba");
    if constexpr (std::is_same_v<T, std::int64_t>) return input_integer(node);
    else if constexpr (std::is_same_v<T, double>) return input_float(node);
    else if constexpr (std::is_same_v<T, bool>) return input_bool(node);
    else if constexpr (std::is_same_v<T, std::string>) return input_string(node);
    else return input_color(node);
}

template <typename T>
bool set_input_value(Document& document, Node& node, const T& value) {
    static_assert(is_input_value<T>::value, "set_input_value<T>: T must be std::int64_t, double, bool, std::string or bark::Rgba");
    if constexpr (std::is_same_v<T, std::int64_t>) return set_input_integer(document, node, value);
    else if constexpr (std::is_same_v<T, double>) return set_input_float(document, node, value);
    else if constexpr (std::is_same_v<T, bool>) return set_input_bool(document, node, value);
    else if constexpr (std::is_same_v<T, std::string>) return set_input_string(document, node, value);
    else return set_input_color(document, node, value);
}

} // namespace grove

#endif // GROVE_INPUT_INPUT_HPP
