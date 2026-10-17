#include "grove/common/marks.hpp"

namespace grove {

const char* to_string(MarkKind kind) noexcept {
    switch (kind) {
        case MarkKind::Tag: return "tag";
        case MarkKind::InputTag: return "input-tag";
        case MarkKind::CustomTag: return "custom-tag";
        case MarkKind::UnknownTag: return "unknown-tag";
        case MarkKind::Property: return "property";
        case MarkKind::UnknownProperty: return "unknown-property";
        case MarkKind::Variable: return "variable";
        case MarkKind::VariableDeclaration: return "variable-declaration";
        case MarkKind::UnresolvedVariable: return "unresolved-variable";
        case MarkKind::SelectorTag: return "selector-tag";
        case MarkKind::UnknownSelectorTag: return "unknown-selector-tag";
        case MarkKind::Color: return "color";
    }
    return "tag";
}

} // namespace grove
