#ifndef GROVE_DOM_TOOLS_HPP
#define GROVE_DOM_TOOLS_HPP

#include "markup/node.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace grove {

Node* query(Node& scope, std::string_view selector, Diagnostics* diagnostics = nullptr);
std::vector<Node*> query_all(Node& scope, std::string_view selector, Diagnostics* diagnostics = nullptr);

bool add_class(Document& document, Node& node, std::string_view name);
bool remove_class(Document& document, Node& node, std::string_view name);
bool toggle_class(Document& document, Node& node, std::string_view name);

std::vector<std::pair<std::string, std::string>> split_style_attribute(std::string_view style);
std::optional<std::string> inline_style(const Node& node, std::string_view property);
void set_inline_style(Document& document, Node& node, std::string_view property, std::string_view value);

} // namespace grove

#endif // GROVE_DOM_TOOLS_HPP
