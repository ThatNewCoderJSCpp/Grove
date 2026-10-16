#ifndef GROVE_BARK_PARSER_HPP
#define GROVE_BARK_PARSER_HPP

#include "stylesheet.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace grove {
namespace bark {

Stylesheet parse_stylesheet(std::string_view source, const std::string& file, Diagnostics& diagnostics, const Position& origin = Position{}, const std::map<std::string, ValueList>& variables = {});

std::vector<Declaration> parse_declarations(std::string_view source, const std::string& file, Diagnostics& diagnostics, const std::map<std::string, ValueList>& variables = {}, const Position& origin = Position{});

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_PARSER_HPP
