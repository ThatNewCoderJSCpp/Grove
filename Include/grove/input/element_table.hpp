#ifndef GROVE_INPUT_ELEMENT_TABLE_HPP
#define GROVE_INPUT_ELEMENT_TABLE_HPP

#include "../markup/node.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace grove {

struct ElementCell {
    int number = 0;
    int row = 0;
    int column = 0;
    int group = 0;
    int period = 0;
    char block = 's';
    double mass = 0.0;
    std::string symbol;
    std::string name;
    std::string category;
    std::string classification;
};

constexpr int k_element_table_columns = 18;
constexpr int k_element_table_rows = 10;

const std::vector<ElementCell>& element_cells();
const ElementCell* element_cell(int number) noexcept;
const ElementCell* element_cell_at(int row, int column) noexcept;
int element_number(std::string_view symbol_or_name) noexcept;
std::string element_token(std::string_view text);

bool element_allowed(const Node& node, int number);
std::vector<int> element_selection(const Node& node);
std::string element_selection_text(const std::vector<int>& numbers);
bool select_element(Document& document, Node& node, int number);
bool set_element_selection(Document& document, Node& node, const std::vector<int>& numbers);
std::uint32_t element_category_color(const ElementCell& cell, std::string_view scheme) noexcept;
std::vector<std::string> element_filter_problems(const Node& node);
int element_token_charge(std::string_view text) noexcept;
std::string element_ion_token(int number, int charge);
std::string element_ion_label(int number, int charge);
bool element_has_charges(const Node& node);
std::vector<int> element_charge_options(const Node& node, int number);
std::vector<std::pair<int, int>> element_ion_selection(const Node& node);
bool select_element_ion(Document& document, Node& node, int number, int charge);

} // namespace grove

#endif // GROVE_INPUT_ELEMENT_TABLE_HPP
