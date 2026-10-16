#ifndef GROVE_MATH_MATH_BLOCK_HPP
#define GROVE_MATH_MATH_BLOCK_HPP

#include "../markup/node.hpp"

#include <string>
#include <vector>

namespace grove {

enum class MathSyntax {
    Infix,
    Brace
};

struct MathBlock {
    std::string source;
    MathSyntax syntax = MathSyntax::Infix;
    bool display = false;
    const Node* element = nullptr;
};

MathSyntax math_syntax_of(const Node& element, const std::string& file, Diagnostics& diagnostics);
std::string math_source_of(const Node& element);
std::string to_brace_form(const MathBlock& block);

bool check_math(const MathBlock& block, const std::string& file, Diagnostics& diagnostics);
std::vector<MathBlock> collect_math(const Document& document, Diagnostics& diagnostics);

} // namespace grove

#endif // GROVE_MATH_MATH_BLOCK_HPP
