#include "grove/input/completion.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace grove {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool segment_start(std::string_view s, std::size_t i) {
    if (i == 0) return true;
    const unsigned char prev = static_cast<unsigned char>(s[i - 1]);
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (prev == '_' || prev == '-' || prev == ':' || prev == '.' || prev == ' ') return true;
    return std::islower(prev) && std::isupper(c);
}

std::string escape_field(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

std::string unescape_field(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

bool is_completion_word_char(char c, std::string_view extra) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '_' || u >= 0x80) return true;
    return extra.find(c) != std::string_view::npos;
}

std::size_t completion_word_start(const std::string& line, std::size_t column, std::string_view extra) {
    std::size_t at = std::min(column, line.size());
    while (at > 0 && is_completion_word_char(line[at - 1], extra)) --at;
    return at;
}

std::optional<std::vector<std::size_t>> completion_match_positions(std::string_view label, std::string_view typed, int& score) {
    std::vector<std::size_t> positions;
    if (typed.empty()) {
        score = 0;
        return positions;
    }
    if (typed.size() > label.size()) return std::nullopt;
    if (label.substr(0, typed.size()) == typed) {
        score = 0;
        for (std::size_t i = 0; i < typed.size(); ++i) positions.push_back(i);
        return positions;
    }
    bool prefix = true;
    for (std::size_t i = 0; i < typed.size() && prefix; ++i) prefix = lower(label[i]) == lower(typed[i]);
    if (prefix) {
        score = 1;
        for (std::size_t i = 0; i < typed.size(); ++i) positions.push_back(i);
        return positions;
    }
    if (lower(label[0]) != lower(typed[0])) {
        bool anywhere = false;
        for (std::size_t i = 1; i < label.size() && !anywhere; ++i) anywhere = segment_start(label, i) && lower(label[i]) == lower(typed[0]);
        if (!anywhere) return std::nullopt;
    }
    std::size_t li = 0;
    bool segments = true;
    for (std::size_t ti = 0; ti < typed.size(); ++ti) {
        const char want = lower(typed[ti]);
        bool found = false;
        if (ti > 0 && li < label.size() && lower(label[li]) == want) {
            positions.push_back(li++);
            continue;
        }
        for (std::size_t k = li; k < label.size(); ++k) {
            if (lower(label[k]) == want && segment_start(label, k)) {
                positions.push_back(k);
                li = k + 1;
                found = true;
                break;
            }
        }
        if (found) continue;
        segments = false;
        for (std::size_t k = li; k < label.size(); ++k) {
            if (lower(label[k]) == want) {
                positions.push_back(k);
                li = k + 1;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    score = segments ? 2 : 3;
    return positions;
}

std::vector<CompletionMatch> filter_completions(const std::vector<CompletionItem>& items, std::string_view typed, std::size_t limit) {
    std::vector<CompletionMatch> out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        int score = 0;
        auto positions = completion_match_positions(items[i].label, typed, score);
        if (!positions) continue;
        out.push_back(CompletionMatch{ i, score, std::move(*positions) });
    }
    std::stable_sort(out.begin(), out.end(), [](const CompletionMatch& a, const CompletionMatch& b) { return a.score < b.score; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::string completions_text(const CompletionList& list) {
    std::string out = std::to_string(list.line) + " " + std::to_string(list.column) + "\n";
    for (const CompletionItem& item : list.items) {
        out += escape_field(item.label) + "\t" + escape_field(item.kind) + "\t" + escape_field(item.detail) + "\t" + escape_field(item.insert) + "\t" + escape_field(item.documentation) + "\n";
    }
    return out;
}

std::optional<CompletionList> parse_completions(const std::string& text) {
    if (text.empty()) return std::nullopt;
    CompletionList list;
    std::istringstream in(text);
    std::string head;
    if (!std::getline(in, head)) return std::nullopt;
    {
        std::istringstream h(head);
        if (!(h >> list.line >> list.column)) return std::nullopt;
    }
    std::string row;
    while (std::getline(in, row)) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty()) continue;
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t tab = row.find('\t', start);
            fields.push_back(unescape_field(row.substr(start, tab == std::string::npos ? std::string::npos : tab - start)));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        CompletionItem item;
        item.label = fields[0];
        if (fields.size() > 1) item.kind = fields[1];
        if (fields.size() > 2) item.detail = fields[2];
        if (fields.size() > 3) item.insert = fields[3];
        if (fields.size() > 4) item.documentation = fields[4];
        if (!item.label.empty()) list.items.push_back(std::move(item));
    }
    return list;
}

std::vector<CompletionItem> word_completions(const std::vector<std::string>& lines, std::size_t line, std::size_t column, std::string_view extra, std::size_t limit) {
    std::vector<CompletionItem> out;
    std::set<std::string> seen;
    std::string current;
    if (line < lines.size()) {
        const std::size_t start = completion_word_start(lines[line], column, extra);
        std::size_t end = std::min(column, lines[line].size());
        while (end < lines[line].size() && is_completion_word_char(lines[line][end], extra)) ++end;
        current = lines[line].substr(start, end - start);
    }
    auto scan = [&](std::size_t li) {
        const std::string& s = lines[li];
        std::size_t i = 0;
        while (i < s.size() && out.size() < limit) {
            if (!is_completion_word_char(s[i], extra) || std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < s.size() && is_completion_word_char(s[j], extra)) ++j;
            std::string word = s.substr(i, j - i);
            if (word.size() >= 2 && word != current && seen.insert(word).second) {
                CompletionItem item;
                item.label = std::move(word);
                item.kind = "word";
                out.push_back(std::move(item));
            }
            i = j;
        }
    };
    if (lines.empty()) return out;
    const std::size_t around = std::min(line, lines.size() - 1);
    for (std::size_t d = 0; d < lines.size() && out.size() < limit; ++d) {
        if (around >= d) scan(around - d);
        if (d > 0 && around + d < lines.size()) scan(around + d);
        if (around < d && around + d >= lines.size()) break;
    }
    return out;
}

std::string completion_insert_text(const CompletionItem& item, std::size_t& caret_offset) {
    std::string text = item.insert.empty() ? item.label : item.insert;
    const std::size_t marker = text.find("$0");
    if (marker != std::string::npos) {
        text.erase(marker, 2);
        caret_offset = marker;
    } else {
        caret_offset = text.size();
    }
    return text;
}

} // namespace grove
