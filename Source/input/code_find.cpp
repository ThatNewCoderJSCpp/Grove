#include "grove/input/code_find.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace grove {

namespace {

bool word_byte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

bool whole_word_at(const std::string& line, std::size_t begin, std::size_t end) {
    if (begin > 0 && word_byte(line[begin - 1]) && word_byte(line[begin])) return false;
    if (end < line.size() && end > begin && word_byte(line[end]) && word_byte(line[end - 1])) return false;
    return true;
}

char fold(char c, bool match_case) {
    return match_case ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool make_regex(std::string_view query, const CodeFindOptions& options, std::regex& out) {
    try {
        auto flags = std::regex::ECMAScript;
        if (!options.match_case) flags |= std::regex::icase;
        out = std::regex(std::string(query), flags);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

} // namespace

std::vector<CodeMatch> find_code_matches(const std::vector<std::string>& lines, std::string_view query, const CodeFindOptions& options, bool* bad_pattern) {
    std::vector<CodeMatch> out;
    if (bad_pattern) *bad_pattern = false;
    if (query.empty()) return out;
    if (options.regex) {
        std::regex re;
        if (!make_regex(query, options, re)) {
            if (bad_pattern) *bad_pattern = true;
            return out;
        }
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const std::string& line = lines[li];
            auto it = std::sregex_iterator(line.begin(), line.end(), re);
            for (; it != std::sregex_iterator(); ++it) {
                const std::size_t begin = static_cast<std::size_t>(it->position(0));
                const std::size_t end = begin + static_cast<std::size_t>(it->length(0));
                if (end == begin) continue;
                if (options.whole_word && !whole_word_at(line, begin, end)) continue;
                out.push_back(CodeMatch{ CodePos{ li, begin }, CodePos{ li, end } });
                if (out.size() >= 100000) return out;
            }
        }
        return out;
    }
    if (query.find('\n') != std::string_view::npos) return out;
    std::string want(query);
    for (char& c : want) c = fold(c, options.match_case);
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        if (line.size() < want.size()) continue;
        std::size_t at = 0;
        while (at + want.size() <= line.size()) {
            bool same = true;
            for (std::size_t k = 0; k < want.size() && same; ++k) same = fold(line[at + k], options.match_case) == want[k];
            if (same && (!options.whole_word || whole_word_at(line, at, at + want.size()))) {
                out.push_back(CodeMatch{ CodePos{ li, at }, CodePos{ li, at + want.size() } });
                if (out.size() >= 100000) return out;
                at += want.size();
                continue;
            }
            ++at;
        }
    }
    return out;
}

std::string code_replacement_for(const std::vector<std::string>& lines, const CodeMatch& match, std::string_view query, std::string_view replacement, const CodeFindOptions& options) {
    if (!options.regex || match.begin.line >= lines.size()) return std::string(replacement);
    std::regex re;
    if (!make_regex(query, options, re)) return std::string(replacement);
    const std::string& line = lines[match.begin.line];
    for (auto it = std::sregex_iterator(line.begin(), line.end(), re); it != std::sregex_iterator(); ++it) {
        if (static_cast<std::size_t>(it->position(0)) == match.begin.column) return it->format(std::string(replacement));
    }
    return std::string(replacement);
}

std::size_t code_match_at_or_after(const std::vector<CodeMatch>& matches, CodePos at) {
    if (matches.empty()) return static_cast<std::size_t>(-1);
    const auto it = std::lower_bound(matches.begin(), matches.end(), at, [](const CodeMatch& m, CodePos p) { return m.begin < p; });
    return it == matches.end() ? 0 : static_cast<std::size_t>(it - matches.begin());
}

std::size_t code_match_before(const std::vector<CodeMatch>& matches, CodePos at) {
    if (matches.empty()) return static_cast<std::size_t>(-1);
    const auto it = std::lower_bound(matches.begin(), matches.end(), at, [](const CodeMatch& m, CodePos p) { return m.begin < p; });
    return it == matches.begin() ? matches.size() - 1 : static_cast<std::size_t>(it - matches.begin()) - 1;
}

} // namespace grove
