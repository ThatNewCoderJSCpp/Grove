#ifndef GROVE_INPUT_CODE_FIND_HPP
#define GROVE_INPUT_CODE_FIND_HPP

#include "code_buffer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct CodeFindOptions {
    bool match_case = false;
    bool whole_word = false;
    bool regex = false;
};

struct CodeMatch {
    CodePos begin;
    CodePos end;
};

std::vector<CodeMatch> find_code_matches(const std::vector<std::string>& lines, std::string_view query, const CodeFindOptions& options, bool* bad_pattern = nullptr);

std::string code_replacement_for(const std::vector<std::string>& lines, const CodeMatch& match, std::string_view query, std::string_view replacement, const CodeFindOptions& options);

std::size_t code_match_at_or_after(const std::vector<CodeMatch>& matches, CodePos at);

std::size_t code_match_before(const std::vector<CodeMatch>& matches, CodePos at);

} // namespace grove

#endif // GROVE_INPUT_CODE_FIND_HPP
