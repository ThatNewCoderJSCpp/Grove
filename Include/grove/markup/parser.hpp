#ifndef GROVE_MARKUP_PARSER_HPP
#define GROVE_MARKUP_PARSER_HPP

#include "node.hpp"

#include <string>
#include <string_view>

namespace grove {

struct MarkupOptions {
    bool warn_unknown_tags = true;
    bool keep_whitespace_text = false;
};

Document parse_markup(std::string_view source, const std::string& file, Diagnostics& diagnostics, const MarkupOptions& options = MarkupOptions());

std::string decode_entities(std::string_view text);
std::string dedent(std::string_view text);

} // namespace grove

#endif // GROVE_MARKUP_PARSER_HPP
