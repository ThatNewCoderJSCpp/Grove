#include "grove/input/code_buffer.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace grove {

namespace {

bool ident_start(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalpha(u) || c == '_';
}

bool ident_char(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) || c == '_';
}

bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

const std::set<std::string>& c_keywords() {
    static const std::set<std::string> k = {
        "auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern", "for", "goto", "if", "inline",
        "register", "restrict", "return", "sizeof", "static", "struct", "switch", "typedef", "union", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local", "alignas", "alignof",
        "static_assert", "thread_local", "typeof",
    };
    return k;
}

const std::set<std::string>& cpp_keywords() {
    static const std::set<std::string> k = [] {
        std::set<std::string> s = c_keywords();
        for (const char* w : { "catch", "class", "concept", "consteval", "constexpr", "constinit", "const_cast", "co_await", "co_return",
                               "co_yield", "decltype", "delete", "dynamic_cast", "explicit", "export", "final", "friend", "import",
                               "module", "mutable", "namespace", "new", "noexcept", "operator", "override", "private", "protected",
                               "public", "reinterpret_cast", "requires", "static_cast", "template", "this", "throw", "try", "typeid",
                               "typename", "using", "virtual" }) s.insert(w);
        return s;
    }();
    return k;
}

const std::set<std::string>& walnut_keywords() {
    static const std::set<std::string> k = {
        "async", "as", "await", "break", "case", "class", "continue", "catch", "concept", "constexpr", "consteval", "constinit",
        "constructor", "const_cast", "co_await", "co_return", "co_yield", "do", "default", "delete", "decltype", "down", "destructor",
        "dynamic", "dynamic_cast", "discard_const", "else", "end", "enum", "export", "extends", "extern", "explicit", "fallthrough",
        "for", "function", "friend", "from", "global", "hidden", "if", "import", "include", "in", "inline", "implicit", "is", "local",
        "mutable", "lazy", "module", "namespace", "new", "noexcept", "operator", "override", "private", "public", "protected", "repeat",
        "reinterpret_cast", "return", "requires", "static", "static_cast", "static_assert", "struct", "switch", "then", "this", "self",
        "throw", "try", "typedef", "template", "typename", "union", "using", "variant", "virtual", "abstract", "volatile", "while",
        "invalidate", "threadsafe", "typeof", "countof", "sizeof", "const",
    };
    return k;
}

const std::set<std::string>& c_types() {
    static const std::set<std::string> k = {
        "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "_Bool", "size_t", "ptrdiff_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t",
        "wchar_t", "char8_t", "char16_t", "char32_t", "FILE",
    };
    return k;
}

const std::set<std::string>& walnut_types() {
    static const std::set<std::string> k = {
        "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "string", "text", "auto", "let",
        "def", "var", "any", "arr", "txt", "str", "special", "List", "function",
    };
    return k;
}

const std::set<std::string>& constants() {
    static const std::set<std::string> k = { "true", "false", "nullptr", "NULL", "nan", "inf", "undefined", "indeterminate" };
    return k;
}

void push(std::vector<CodeToken>& out, std::size_t begin, std::size_t end, CodeTokenKind kind) {
    if (end <= begin) return;
    if (!out.empty() && out.back().kind == kind && out.back().end == begin) {
        out.back().end = end;
        return;
    }
    out.push_back(CodeToken{ begin, end, kind });
}

std::size_t skip_string(std::string_view line, std::size_t i, char quote) {
    ++i;
    while (i < line.size()) {
        if (line[i] == '\\') {
            i += 2;
            continue;
        }
        if (line[i] == quote) return i + 1;
        ++i;
    }
    return line.size();
}

std::size_t skip_number(std::string_view line, std::size_t i) {
    while (i < line.size() && (ident_char(line[i]) || line[i] == '.' || line[i] == '\'' ||
                               ((line[i] == '+' || line[i] == '-') && i > 0 && (line[i - 1] == 'e' || line[i - 1] == 'E' || line[i - 1] == 'p' || line[i - 1] == 'P')))) {
        ++i;
    }
    return i;
}

std::vector<CodeToken> highlight_c_like(CodeLanguage language, std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    const std::set<std::string>& keywords = language == CodeLanguage::C ? c_keywords() : language == CodeLanguage::Walnut ? walnut_keywords() : cpp_keywords();
    const std::set<std::string>& types = language == CodeLanguage::Walnut ? walnut_types() : c_types();
    std::size_t i = 0;
    if (state.block_comment) {
        const std::size_t close = line.find("*/");
        if (close == std::string_view::npos) {
            push(out, 0, line.size(), CodeTokenKind::Comment);
            return out;
        }
        push(out, 0, close + 2, CodeTokenKind::Comment);
        state.block_comment = false;
        i = close + 2;
    }
    std::size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
    if (i <= first && first < line.size() && line[first] == '#') {
        std::size_t end = first + 1;
        while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
        while (end < line.size() && ident_char(line[end])) ++end;
        push(out, first, end, CodeTokenKind::Preprocessor);
        i = end;
        std::size_t rest = i;
        while (rest < line.size() && (line[rest] == ' ' || line[rest] == '\t')) ++rest;
        if (rest < line.size() && line[rest] == '<') {
            const std::size_t close = line.find('>', rest);
            if (close != std::string_view::npos) {
                push(out, rest, close + 1, CodeTokenKind::String);
                i = close + 1;
            }
        }
    }
    while (i < line.size()) {
        const char c = line[i];
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            push(out, i, line.size(), CodeTokenKind::Comment);
            break;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const std::size_t close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                push(out, i, line.size(), CodeTokenKind::Comment);
                state.block_comment = true;
                break;
            }
            push(out, i, close + 2, CodeTokenKind::Comment);
            i = close + 2;
            continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && language == CodeLanguage::Walnut)) {
            const std::size_t end = skip_string(line, i, c);
            push(out, i, end, CodeTokenKind::String);
            i = end;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            const std::size_t end = skip_number(line, i);
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string word(line.substr(i, end - i));
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (constants().count(word)) kind = CodeTokenKind::Constant;
            else if (keywords.count(word)) kind = CodeTokenKind::Keyword;
            else if (types.count(word)) kind = CodeTokenKind::Type;
            else {
                std::size_t next = end;
                while (next < line.size() && line[next] == ' ') ++next;
                if (next < line.size() && line[next] == '(') kind = CodeTokenKind::Function;
                else if (word.size() > 1 && std::all_of(word.begin(), word.end(), [](char ch) { return (ch >= 'A' && ch <= 'Z') || ch == '_' || digit(ch); })) kind = CodeTokenKind::Constant;
                else if (word[0] >= 'A' && word[0] <= 'Z') kind = CodeTokenKind::Type;
            }
            push(out, i, end, kind);
            i = end;
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(c))) {
            const bool bracket = c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == ';' || c == ',';
            push(out, i, i + 1, bracket ? CodeTokenKind::Plain : CodeTokenKind::Operator);
            ++i;
            continue;
        }
        push(out, i, i + 1, CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

std::vector<CodeToken> highlight_markup(std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (state.markup_comment) {
            const std::size_t close = line.find("-->", i);
            if (close == std::string_view::npos) {
                push(out, i, line.size(), CodeTokenKind::Comment);
                return out;
            }
            push(out, i, close + 3, CodeTokenKind::Comment);
            state.markup_comment = false;
            i = close + 3;
            continue;
        }
        if (line.compare(i, 4, "<!--") == 0) {
            state.markup_comment = true;
            continue;
        }
        const char c = line[i];
        if (c == '<') {
            std::size_t j = i + 1;
            if (j < line.size() && (line[j] == '/' || line[j] == '!')) ++j;
            push(out, i, j, CodeTokenKind::Operator);
            std::size_t end = j;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
            push(out, j, end, CodeTokenKind::Tag);
            i = end;
            while (i < line.size() && line[i] != '>') {
                const char a = line[i];
                if (a == '"' || a == '\'') {
                    const std::size_t e = skip_string(line, i, a);
                    push(out, i, e, CodeTokenKind::String);
                    i = e;
                } else if (ident_start(a)) {
                    std::size_t e = i;
                    while (e < line.size() && (ident_char(line[e]) || line[e] == '-')) ++e;
                    push(out, i, e, CodeTokenKind::Attribute);
                    i = e;
                } else {
                    push(out, i, i + 1, a == '=' || a == '/' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
                    ++i;
                }
            }
            if (i < line.size()) {
                push(out, i, i + 1, CodeTokenKind::Operator);
                ++i;
            }
            continue;
        }
        if (c == '&') {
            const std::size_t semi = line.find(';', i);
            if (semi != std::string_view::npos && semi - i <= 10) {
                push(out, i, semi + 1, CodeTokenKind::Constant);
                i = semi + 1;
                continue;
            }
        }
        push(out, i, i + 1, CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

std::vector<CodeToken> highlight_bark(std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    std::size_t i = 0;
    if (state.block_comment) {
        const std::size_t close = line.find("*/");
        if (close == std::string_view::npos) {
            push(out, 0, line.size(), CodeTokenKind::Comment);
            return out;
        }
        push(out, 0, close + 2, CodeTokenKind::Comment);
        state.block_comment = false;
        i = close + 2;
    }
    const bool selector_line = line.find('{') != std::string_view::npos;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            push(out, i, line.size(), CodeTokenKind::Comment);
            break;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const std::size_t close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                push(out, i, line.size(), CodeTokenKind::Comment);
                state.block_comment = true;
                break;
            }
            push(out, i, close + 2, CodeTokenKind::Comment);
            i = close + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = skip_string(line, i, c);
            push(out, i, end, CodeTokenKind::String);
            i = end;
            continue;
        }
        if (c == '$' || (c == '#' && !selector_line)) {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
            push(out, i, end, CodeTokenKind::Constant);
            i = end;
            continue;
        }
        if ((c == '.' || c == '#') && selector_line) {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
            push(out, i, end, CodeTokenKind::Attribute);
            i = end;
            continue;
        }
        if (digit(c) || ((c == '-' || c == '.') && i + 1 < line.size() && digit(line[i + 1]))) {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '.' || line[end] == '%')) ++end;
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (ident_start(c) || c == '-') {
            std::size_t end = i;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
            if (end == i) {
                push(out, i, i + 1, CodeTokenKind::Operator);
                ++i;
                continue;
            }
            std::size_t next = end;
            while (next < line.size() && line[next] == ' ') ++next;
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (selector_line) kind = (i > 0 && line[i - 1] == ':') ? CodeTokenKind::Keyword : CodeTokenKind::Tag;
            else if (next < line.size() && line[next] == ':') kind = CodeTokenKind::Property;
            else if (next < line.size() && line[next] == '(') kind = CodeTokenKind::Function;
            else kind = CodeTokenKind::Keyword;
            push(out, i, end, kind);
            i = end;
            continue;
        }
        push(out, i, i + 1, std::ispunct(static_cast<unsigned char>(c)) && c != ';' && c != '{' && c != '}' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

} // namespace

const char* to_string(CodeLanguage language) noexcept {
    switch (language) {
        case CodeLanguage::Plain: return "plain";
        case CodeLanguage::C: return "c";
        case CodeLanguage::Cpp: return "cpp";
        case CodeLanguage::Walnut: return "walnut";
        case CodeLanguage::Grove: return "grove";
        case CodeLanguage::Bark: return "bark";
    }
    return "plain";
}

const char* to_string(CodeTokenKind kind) noexcept {
    switch (kind) {
        case CodeTokenKind::Plain: return "plain";
        case CodeTokenKind::Keyword: return "keyword";
        case CodeTokenKind::Type: return "type";
        case CodeTokenKind::String: return "string";
        case CodeTokenKind::Number: return "number";
        case CodeTokenKind::Comment: return "comment";
        case CodeTokenKind::Preprocessor: return "preprocessor";
        case CodeTokenKind::Function: return "function";
        case CodeTokenKind::Operator: return "operator";
        case CodeTokenKind::Tag: return "tag";
        case CodeTokenKind::Attribute: return "attribute";
        case CodeTokenKind::Property: return "property";
        case CodeTokenKind::Constant: return "constant";
    }
    return "plain";
}

CodeLanguage code_language_from_name(std::string_view name) noexcept {
    const std::string n = lower(name);
    if (n == "c" || n == "h") return CodeLanguage::C;
    if (n == "cpp" || n == "c++" || n == "cxx" || n == "cc" || n == "hpp" || n == "hh" || n == "hxx") return CodeLanguage::Cpp;
    if (n == "walnut" || n == "wal" || n == "walh" || n == "hwal") return CodeLanguage::Walnut;
    if (n == "grove" || n == "html" || n == "xml" || n == "markup") return CodeLanguage::Grove;
    if (n == "bark" || n == "css") return CodeLanguage::Bark;
    return CodeLanguage::Plain;
}

CodeLanguage code_language_for_path(std::string_view path) noexcept {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return CodeLanguage::Plain;
    return code_language_from_name(name.substr(dot + 1));
}

std::vector<CodeToken> highlight_code_line(CodeLanguage language, std::string_view line, CodeLineState& state) {
    switch (language) {
        case CodeLanguage::C:
        case CodeLanguage::Cpp:
        case CodeLanguage::Walnut: return highlight_c_like(language, line, state);
        case CodeLanguage::Grove: return highlight_markup(line, state);
        case CodeLanguage::Bark: return highlight_bark(line, state);
        case CodeLanguage::Plain: break;
    }
    std::vector<CodeToken> out;
    if (!line.empty()) out.push_back(CodeToken{ 0, line.size(), CodeTokenKind::Plain });
    return out;
}

} // namespace grove
