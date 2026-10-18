#include "grove/bark/properties.hpp"

#include <algorithm>

namespace grove {
namespace bark {

const std::vector<std::string_view>& known_properties() noexcept {
    static const std::vector<std::string_view> names = {
        "accent-color",
        "align",
        "align-items",
        "align-self",
        "appearance",
        "backdrop",
        "background",
        "border-color",
        "border-radius",
        "border-width",
        "code-attribute",
        "code-builtin-operator",
        "code-builtin-type",
        "code-cast",
        "code-comment",
        "code-constant",
        "code-control-keyword",
        "code-declaration",
        "code-enum-member",
        "code-error-line",
        "code-error-underline",
        "code-escape",
        "code-function",
        "code-hover",
        "code-keyword",
        "code-mark",
        "code-namespace",
        "code-number",
        "code-operator",
        "code-overloaded-operator",
        "code-plain",
        "code-preprocessor",
        "code-property",
        "code-self",
        "code-string",
        "code-tag",
        "code-template-scope",
        "code-this",
        "code-type",
        "code-unresolved",
        "code-variable",
        "code-variable-template",
        "code-warning-underline",
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
