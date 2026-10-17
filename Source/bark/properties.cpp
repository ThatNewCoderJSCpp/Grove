#include "grove/bark/properties.hpp"

#include <algorithm>

namespace grove {
namespace bark {

const std::vector<std::string_view>& known_properties() noexcept {
    static const std::vector<std::string_view> names = {
        "accent-color",
        "appearance",
        "background",
        "border-color",
        "border-radius",
        "border-width",
        "color",
        "cursor",
        "display",
        "flex",
        "font",
        "font-family",
        "font-size",
        "font-style",
        "font-weight",
        "gap",
        "height",
        "justify-content",
        "letter-spacing",
        "line-height",
        "margin",
        "math-color",
        "math-syntax",
        "max-height",
        "max-width",
        "min-height",
        "min-width",
        "opacity",
        "overflow",
        "padding",
        "size",
        "text-align",
        "text-decoration",
        "title",
        "white-space",
        "width",
    };
    return names;
}

bool is_known_property(std::string_view name) noexcept {
    const auto& names = known_properties();
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace bark
} // namespace grove
