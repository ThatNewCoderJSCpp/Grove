#ifndef GROVE_SCRIPT_SCRIPT_HPP
#define GROVE_SCRIPT_SCRIPT_HPP

#include "../markup/node.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

enum class ScriptLanguage {
    Walnut,
    WalnutHeader,
    Cpp,
    CppHeader,
    C,
    CHeader
};

struct ScriptBlock {
    ScriptLanguage language = ScriptLanguage::Walnut;
    std::string source_path;
    std::string code;
    bool inline_code = false;
    std::string entry;
    Span span;
    const Node* element = nullptr;
};

const char* to_string(ScriptLanguage language) noexcept;
const char* file_extension(ScriptLanguage language) noexcept;
std::optional<ScriptLanguage> language_from_name(std::string_view name) noexcept;
std::optional<ScriptLanguage> language_from_path(std::string_view path) noexcept;
bool is_header(ScriptLanguage language) noexcept;

std::vector<ScriptBlock> collect_scripts(const Document& document, const std::string& base_dir, Diagnostics& diagnostics);

} // namespace grove

#endif // GROVE_SCRIPT_SCRIPT_HPP
