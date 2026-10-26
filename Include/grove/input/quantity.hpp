#ifndef GROVE_INPUT_QUANTITY_HPP
#define GROVE_INPUT_QUANTITY_HPP

#include "../markup/events.hpp"
#include "../markup/node.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

bool is_quantity_tag(std::string_view tag) noexcept;
bool is_quantity(const Node& node) noexcept;
Node* quantity_of(Node& node) noexcept;
const Node* quantity_of(const Node& node) noexcept;

std::string quantity_dimension(const Node& node);
std::string quantity_unit(const Node& node);
std::vector<std::string> quantity_units(const Node& node);
std::optional<double> quantity_value(const Node& node);
std::optional<double> quantity_si(const Node& node);
std::string quantity_text(const Node& node);

bool set_quantity_si(Document& document, Node& node, double value_si);
bool set_quantity_value(Document& document, Node& node, double value, std::string_view unit = std::string_view());
bool set_quantity_text(Document& document, Node& node, std::string_view text);
bool set_quantity_unit(Document& document, Node& node, std::string_view unit);

bool expand_quantity(Document& document, Node& node);
std::size_t expand_quantity_inputs(Document& document);
bool quantity_event(Document& document, Node& target, const Event& event);
std::vector<std::string> quantity_problems(const Node& node);

} // namespace grove

#endif // GROVE_INPUT_QUANTITY_HPP
