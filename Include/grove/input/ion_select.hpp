#ifndef GROVE_INPUT_ION_SELECT_HPP
#define GROVE_INPUT_ION_SELECT_HPP

#include "../markup/node.hpp"
#include "input.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct IonChoice {
    std::string name;
    std::string formula;
    std::string formula_text;
    std::string category;
    int charge = 0;
    bool polyatomic = true;
};

bool is_ion_select_tag(std::string_view tag) noexcept;
bool is_ion_select(const Node& node) noexcept;
const std::vector<IonChoice>& ion_choices();
std::vector<IonChoice> ion_choices_for(const Node& node);
std::vector<SelectOption> ion_select_options(const Node& node);
std::vector<std::string> ion_select_problems(const Node& node);
bool is_acid_base_select_tag(std::string_view tag) noexcept;
bool is_acid_base_select(const Node& node) noexcept;
std::vector<SelectOption> acid_base_select_options(const Node& node);
std::vector<std::string> acid_base_select_problems(const Node& node);

} // namespace grove

#endif // GROVE_INPUT_ION_SELECT_HPP
