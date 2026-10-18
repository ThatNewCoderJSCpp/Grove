#ifndef GROVE_PAGES_HPP
#define GROVE_PAGES_HPP

#include "markup/node.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace grove {

bool is_page(const Node& node) noexcept;
bool is_modal(const Node& node) noexcept;
bool is_page_current(const Node& page);
bool is_page_shown(const Node& node);
bool is_navigation_current(const Node& node);
const Node* find_in_tree(const Node& any, std::string_view id);
Node* find_page(Document& document, std::string_view target);
Node* current_page(Node& group);

bool show_page(Document& document, Node& page);
bool go_back(Document& document);
bool open_modal(Document& document, Node& modal);
bool close_modal(Document& document, Node& modal);
bool navigate(Document& document, std::string_view target, Node* from = nullptr);
bool follow_link(Document& document, Node* clicked);
std::vector<Node*> open_modals(Node& scope);

} // namespace grove

#endif // GROVE_PAGES_HPP
