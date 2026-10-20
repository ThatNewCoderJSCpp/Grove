#ifndef GROVE_INPUT_COMPLETION_HPP
#define GROVE_INPUT_COMPLETION_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct CompletionItem {
    std::string label;
    std::string kind;
    std::string detail;
    std::string insert;
    std::string documentation;
    std::string preview;
};

struct CompletionList {
    std::size_t line = 0;
    std::size_t column = 0;
    std::vector<CompletionItem> items;
};

struct CompletionMatch {
    std::size_t index = 0;
    int score = 0;
    std::vector<std::size_t> positions;
};

bool is_completion_word_char(char c, std::string_view extra = {});
std::size_t completion_word_start(const std::string& line, std::size_t column, std::string_view extra = {});
std::optional<std::vector<std::size_t>> completion_match_positions(std::string_view label, std::string_view typed, int& score);
std::vector<CompletionMatch> filter_completions(const std::vector<CompletionItem>& items, std::string_view typed, std::size_t limit = 500);

std::string completions_text(const CompletionList& list);
std::optional<CompletionList> parse_completions(const std::string& text);

std::vector<CompletionItem> word_completions(const std::vector<std::string>& lines, std::size_t line, std::size_t column, std::string_view extra = {}, std::size_t limit = 2000);

std::string completion_insert_text(const CompletionItem& item, std::size_t& caret_offset);

} // namespace grove

#endif // GROVE_INPUT_COMPLETION_HPP
