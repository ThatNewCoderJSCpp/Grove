#include "grove/markup/tags.hpp"

namespace grove {

namespace {

constexpr TagInfo k_tags[] = {
    { "grove",    TagContent::Normal, TagRole::Structure },
    { "head",     TagContent::Normal, TagRole::Structure },
    { "body",     TagContent::Normal, TagRole::Structure },
    { "title",    TagContent::Normal, TagRole::Structure },
    { "window",   TagContent::Normal, TagRole::Window },
    { "panel",    TagContent::Normal, TagRole::Block },
    { "div",      TagContent::Normal, TagRole::Block },
    { "p",        TagContent::Normal, TagRole::Block },
    { "h1",       TagContent::Normal, TagRole::Block },
    { "h2",       TagContent::Normal, TagRole::Block },
    { "h3",       TagContent::Normal, TagRole::Block },
    { "h4",       TagContent::Normal, TagRole::Block },
    { "row",      TagContent::Normal, TagRole::Block },
    { "column",   TagContent::Normal, TagRole::Block },
    { "list",     TagContent::Normal, TagRole::Block },
    { "item",     TagContent::Normal, TagRole::Block },
    { "text",     TagContent::Normal, TagRole::Inline },
    { "span",     TagContent::Normal, TagRole::Inline },
    { "b",        TagContent::Normal, TagRole::Inline },
    { "i",        TagContent::Normal, TagRole::Inline },
    { "u",        TagContent::Normal, TagRole::Inline },
    { "code",     TagContent::Normal, TagRole::Inline },
    { "link",     TagContent::Normal, TagRole::Inline },
    { "button",   TagContent::Normal, TagRole::Control },
    { "label",    TagContent::Normal, TagRole::Control },
    { "input",    TagContent::Void,   TagRole::Control },
    { "input_integer", TagContent::Void,   TagRole::Control },
    { "input_float",   TagContent::Void,   TagRole::Control },
    { "input_string",  TagContent::Void,   TagRole::Control },
    { "input_bool",    TagContent::Normal, TagRole::Control },
    { "input_radio",   TagContent::Normal, TagRole::Control },
    { "input_slider",  TagContent::Void,   TagRole::Control },
    { "input_color",   TagContent::Void,   TagRole::Control },
    { "checkbox", TagContent::Void,   TagRole::Control },
    { "slider",   TagContent::Void,   TagRole::Control },
    { "color",    TagContent::Void,   TagRole::Control },
    { "img",      TagContent::Void,   TagRole::Inline },
    { "br",       TagContent::Void,   TagRole::Inline },
    { "hr",       TagContent::Void,   TagRole::Block },
    { "math",     TagContent::Raw,    TagRole::Math },
    { "script",   TagContent::Raw,    TagRole::Script },
    { "style",    TagContent::Raw,    TagRole::Style },
};

} // namespace

const TagInfo* find_tag(std::string_view name) noexcept {
    for (const TagInfo& t : k_tags) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

TagContent tag_content(std::string_view name) noexcept {
    const TagInfo* t = find_tag(name);
    return t ? t->content : TagContent::Normal;
}

TagRole tag_role(std::string_view name) noexcept {
    const TagInfo* t = find_tag(name);
    return t ? t->role : TagRole::Unknown;
}

bool is_known_tag(std::string_view name) noexcept {
    return find_tag(name) != nullptr;
}

} // namespace grove
