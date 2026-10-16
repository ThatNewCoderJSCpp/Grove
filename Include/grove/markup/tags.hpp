#ifndef GROVE_MARKUP_TAGS_HPP
#define GROVE_MARKUP_TAGS_HPP

#include <string_view>

namespace grove {

enum class TagContent {
    Normal,
    Raw,
    Void
};

enum class TagRole {
    Unknown,
    Structure,
    Window,
    Block,
    Inline,
    Math,
    Script,
    Style,
    Control
};

struct TagInfo {
    std::string_view name;
    TagContent content = TagContent::Normal;
    TagRole role = TagRole::Unknown;
};

const TagInfo* find_tag(std::string_view name) noexcept;
TagContent tag_content(std::string_view name) noexcept;
TagRole tag_role(std::string_view name) noexcept;
bool is_known_tag(std::string_view name) noexcept;

} // namespace grove

#endif // GROVE_MARKUP_TAGS_HPP
