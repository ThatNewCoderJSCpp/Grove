#ifndef GROVE_COMMON_MARKS_HPP
#define GROVE_COMMON_MARKS_HPP

#include "diagnostics.hpp"

#include <cstdint>
#include <vector>

namespace grove {

enum class MarkKind {
    Tag,
    InputTag,
    CustomTag,
    UnknownTag,
    Property,
    UnknownProperty,
    Variable,
    VariableDeclaration,
    UnresolvedVariable,
    SelectorTag,
    UnknownSelectorTag,
    Color
};

struct Mark {
    MarkKind kind = MarkKind::Tag;
    Span span;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

using Marks = std::vector<Mark>;

const char* to_string(MarkKind kind) noexcept;

} // namespace grove

#endif // GROVE_COMMON_MARKS_HPP
