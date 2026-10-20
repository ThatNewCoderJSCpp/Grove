#include "grove/input/code_buffer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

[[maybe_unused]] const std::set<std::string>& walnut_keywords() {
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

[[maybe_unused]] const std::set<std::string>& walnut_types() {
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

const std::set<std::string>& cpp_types() {
    static const std::set<std::string> k = [] {
        std::set<std::string> s = c_types();
        for (const char* w : { "nullptr_t", "char8_t" }) s.insert(w);
        return s;
    }();
    return k;
}

const std::set<std::string>& std_types() {
    static const std::set<std::string> k = {
        "string", "string_view", "wstring", "u8string", "u16string", "u32string", "vector", "array", "map", "multimap", "unordered_map",
        "set", "multiset", "unordered_set", "list", "forward_list", "deque", "queue", "priority_queue", "stack", "pair", "tuple",
        "optional", "variant", "any", "function", "unique_ptr", "shared_ptr", "weak_ptr", "span", "byte", "size_t", "ptrdiff_t",
        "nullptr_t", "ostream", "istream", "iostream", "ostringstream", "istringstream", "stringstream", "ofstream", "ifstream",
        "fstream", "thread", "jthread", "mutex", "recursive_mutex", "lock_guard", "unique_lock", "scoped_lock", "condition_variable",
        "atomic", "future", "promise", "initializer_list", "bitset", "complex", "valarray", "regex", "error_code", "exception",
        "runtime_error", "logic_error", "invalid_argument", "out_of_range", "path", "filesystem_error", "int8_t", "int16_t", "int32_t",
        "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t",
    };
    return k;
}

const std::set<std::string>& string_prefixes() {
    static const std::set<std::string> k = { "L", "u", "U", "u8" };
    return k;
}

bool all_upper(const std::string& word) {
    bool letter = false;
    for (char ch : word) {
        if (ch >= 'A' && ch <= 'Z') letter = true;
        else if (ch != '_' && !digit(ch)) return false;
    }
    return letter && word.size() > 1;
}

std::size_t skip_spaces(std::string_view line, std::size_t i) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i;
}

void push_quoted(std::vector<CodeToken>& out, std::string_view line, std::size_t begin, std::size_t end) {
    std::size_t run = begin;
    for (std::size_t i = begin; i < end; ++i) {
        if (line[i] != '\\' || i + 1 >= end) continue;
        std::size_t e = i + 2;
        const char n = line[i + 1];
        if (n == 'x') {
            while (e < end && std::isxdigit(static_cast<unsigned char>(line[e]))) ++e;
        } else if ((n == 'u' || n == 's') && e < end && line[e] == '{') {
            const std::size_t close = line.find('}', e);
            e = close == std::string_view::npos || close >= end ? end : close + 1;
        } else if (n == 'u') {
            e = i + 6;
        } else if (n == 'U') {
            e = i + 10;
        } else if (n >= '0' && n <= '7') {
            while (e < end && e < i + 4 && line[e] >= '0' && line[e] <= '7') ++e;
        }
        e = std::min(e, end);
        push(out, run, i, CodeTokenKind::String);
        push(out, i, e, CodeTokenKind::Escape);
        run = e;
        i = e - 1;
    }
    push(out, run, end, CodeTokenKind::String);
}

const std::set<std::string>& c_control() {
    static const std::set<std::string> k = {
        "break", "case", "catch", "co_await", "co_return", "co_yield", "continue", "default", "do", "else", "for", "goto", "if", "return",
        "switch", "throw", "try", "while",
    };
    return k;
}

const std::set<std::string>& c_primitives() {
    static const std::set<std::string> k = {
        "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "_Bool", "wchar_t", "char8_t", "char16_t",
        "char32_t", "auto",
    };
    return k;
}

const std::set<std::string>& c_casts() {
    static const std::set<std::string> k = { "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "bit_cast" };
    return k;
}

CodeTokenKind refine_c_kind(CodeTokenKind kind, const std::string& word) {
    if (kind == CodeTokenKind::Keyword) {
        if (c_control().count(word)) return CodeTokenKind::ControlKeyword;
        if (word == "this") return CodeTokenKind::ThisKeyword;
        if (c_casts().count(word)) return CodeTokenKind::Cast;
        if (word == "auto") return CodeTokenKind::BuiltinType;
    }
    if (kind == CodeTokenKind::Type && c_primitives().count(word)) return CodeTokenKind::BuiltinType;
    return kind;
}

std::vector<CodeToken> highlight_c_like(CodeLanguage language, std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    const bool walnut = language == CodeLanguage::Walnut;
    const std::set<std::string>& keywords = language == CodeLanguage::C ? c_keywords() : walnut ? walnut_keywords() : cpp_keywords();
    const std::set<std::string>& types = walnut ? walnut_types() : language == CodeLanguage::Cpp ? cpp_types() : c_types();
    std::size_t i = 0;
    if (state.raw_string) {
        const std::string close = ")" + state.raw_delimiter + "\"";
        const std::size_t at = line.find(close);
        if (at == std::string_view::npos) {
            push(out, 0, line.size(), CodeTokenKind::String);
            return out;
        }
        push(out, 0, at + close.size(), CodeTokenKind::String);
        state.raw_string = false;
        state.raw_delimiter.clear();
        i = at + close.size();
    }
    if (state.block_comment) {
        const std::size_t close = line.find("*/", i);
        if (close == std::string_view::npos) {
            push(out, i, line.size(), CodeTokenKind::Comment);
            return out;
        }
        push(out, i, close + 2, CodeTokenKind::Comment);
        state.block_comment = false;
        i = close + 2;
    }
    bool define_name = false;
    const std::size_t first = skip_spaces(line, 0);
    if (i <= first && first < line.size() && line[first] == '#') {
        std::size_t end = skip_spaces(line, first + 1);
        const std::size_t word = end;
        while (end < line.size() && ident_char(line[end])) ++end;
        push(out, first, end, CodeTokenKind::Preprocessor);
        define_name = line.substr(word, end - word) == "define" || line.substr(word, end - word) == "undef" ||
                      line.substr(word, end - word) == "ifdef" || line.substr(word, end - word) == "ifndef";
        i = end;
        const std::size_t rest = skip_spaces(line, i);
        if (rest < line.size() && line[rest] == '<') {
            const std::size_t close = line.find('>', rest);
            if (close != std::string_view::npos) {
                push(out, rest, close + 1, CodeTokenKind::String);
                i = close + 1;
            }
        }
    }
    bool in_attribute = false;
    bool after_std = false;
    CodeTokenKind next_name = CodeTokenKind::Plain;
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
        if (c == '"' || c == '\'' || (c == '`' && walnut)) {
            const std::size_t end = skip_string(line, i, c);
            push_quoted(out, line, i, end);
            i = end;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            const std::size_t end = skip_number(line, i);
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (!walnut && c == '[' && i + 1 < line.size() && line[i + 1] == '[') {
            push(out, i, i + 2, CodeTokenKind::Plain);
            in_attribute = true;
            i += 2;
            continue;
        }
        if (in_attribute && c == ']' && i + 1 < line.size() && line[i + 1] == ']') {
            push(out, i, i + 2, CodeTokenKind::Plain);
            in_attribute = false;
            i += 2;
            continue;
        }
        if (ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string word(line.substr(i, end - i));
            if (!walnut && end < line.size()) {
                const bool raw = (word == "R" || word == "LR" || word == "uR" || word == "UR" || word == "u8R") && line[end] == '"';
                if (raw) {
                    const std::size_t open = line.find('(', end + 1);
                    if (open != std::string_view::npos && open - end - 1 <= 16) {
                        const std::string delimiter(line.substr(end + 1, open - end - 1));
                        const std::string close = ")" + delimiter + "\"";
                        const std::size_t at = line.find(close, open + 1);
                        if (at == std::string_view::npos) {
                            push(out, i, line.size(), CodeTokenKind::String);
                            state.raw_string = true;
                            state.raw_delimiter = delimiter;
                            break;
                        }
                        push(out, i, at + close.size(), CodeTokenKind::String);
                        i = at + close.size();
                        continue;
                    }
                }
                if (string_prefixes().count(word) && (line[end] == '"' || line[end] == '\'')) {
                    const std::size_t close = skip_string(line, end, line[end]);
                    push(out, i, end, CodeTokenKind::String);
                    push_quoted(out, line, end, close);
                    i = close;
                    continue;
                }
            }
            const std::size_t next = skip_spaces(line, end);
            const bool scope = next + 1 < line.size() && line[next] == ':' && line[next + 1] == ':';
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (in_attribute) kind = CodeTokenKind::Attribute;
            else if (define_name) kind = end < line.size() && line[end] == '(' ? CodeTokenKind::Function : CodeTokenKind::Constant;
            else if (constants().count(word)) kind = CodeTokenKind::Constant;
            else if (keywords.count(word)) kind = CodeTokenKind::Keyword;
            else if (types.count(word)) kind = CodeTokenKind::Type;
            else if (next_name != CodeTokenKind::Plain) kind = next_name;
            else if (scope) kind = CodeTokenKind::Namespace;
            else if (after_std && std_types().count(word)) kind = CodeTokenKind::Type;
            else if (next < line.size() && line[next] == '(') kind = CodeTokenKind::Function;
            else if (all_upper(word)) kind = CodeTokenKind::Constant;
            else if (word[0] >= 'A' && word[0] <= 'Z') kind = CodeTokenKind::Type;
            define_name = false;
            const bool keyword = kind == CodeTokenKind::Keyword;
            if (!walnut) kind = refine_c_kind(kind, word);
            if (!keyword) next_name = CodeTokenKind::Plain;
            if (word == "class" || word == "struct" || word == "enum" || word == "union" || word == "concept" || word == "typename") next_name = CodeTokenKind::Type;
            else if (word == "namespace" || word == "module") next_name = CodeTokenKind::Namespace;
            else if (keyword && word != "enum") next_name = CodeTokenKind::Plain;
            after_std = scope && word == "std";
            push(out, i, end, kind);
            i = end;
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(c))) {
            const bool bracket = c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == ';' || c == ',';
            if (c == ':' && i + 1 < line.size() && line[i + 1] == ':') {
                push(out, i, i + 2, CodeTokenKind::Operator);
                i += 2;
                continue;
            }
            if (c != '*' && c != '&') after_std = false;
            push(out, i, i + 1, bracket ? CodeTokenKind::Plain : CodeTokenKind::Operator);
            ++i;
            continue;
        }
        push(out, i, i + 1, CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

const std::set<std::string>& walnut_control() {
    static const std::set<std::string> k = {
        "fallthrough", "co_return", "co_yield", "continue", "default", "repeat", "return", "switch", "await", "break", "catch", "label",
        "throw", "while", "case", "down", "else", "goto", "then", "end", "for", "try", "as", "do", "if", "in", "is",
    };
    return k;
}

const std::set<std::string>& walnut_storage() {
    static const std::set<std::string> k = {
        "constructor", "destructor", "namespace", "function", "inherits", "operator", "requires", "template", "typename", "concept",
        "extends", "include", "inherit", "typedef", "define", "export", "import", "module", "struct", "class", "union", "using", "enum",
        "def", "let", "thread_local", "threadlocal", "no_discard", "threadsafe", "automatic", "consteval", "constexpr", "constinit",
        "nodiscard", "protected", "abstract", "constant", "explicit", "implicit", "noexcept", "override", "volatile", "hoisted", "mutable",
        "private", "virtual", "extern", "friend", "global", "hidden", "inline", "public", "static", "async", "const", "hoist", "local",
        "lazy",
    };
    return k;
}

const std::set<std::string>& walnut_primitives() {
    static const std::set<std::string> k = {
        "character", "unsigned", "variable", "boolean", "decimal", "dynamic", "floater", "integer", "special", "variant", "double", "string",
        "array", "float", "short", "auto", "bool", "char", "long", "text", "void", "any", "arr", "int", "str", "txt", "var",
    };
    return k;
}

const std::set<std::string>& walnut_constants() {
    static const std::set<std::string> k = {
        "indeterminate", "undefined", "infinity", "nullptr", "FALSE", "False", "false", "TRUE", "True", "true", "inf", "nan",
    };
    return k;
}

const std::set<std::string>& walnut_casts() {
    static const std::set<std::string> k = { "reinterpret_cast", "dynamic_cast", "static_cast", "const_cast", "bit_cast", "bitcast" };
    return k;
}

const std::set<std::string>& walnut_word_operators() {
    static const std::set<std::string> k = {
        "discard_no_discard", "discard_nodiscard", "discard_const", "static_assert", "align_as", "align_of", "count_of", "decltype",
        "alignas", "alignof", "countof", "size_of", "type_id", "type_of", "delete", "sizeof", "typeid", "typeof", "new",
    };
    return k;
}

bool ascii_ident_start(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool ascii_ident_char(char c) noexcept { return ascii_ident_start(c) || digit(c); }

bool hex_digit(char c) noexcept { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

std::size_t word_end(std::string_view line, std::size_t i) {
    while (i < line.size() && ascii_ident_char(line[i])) ++i;
    return i;
}

std::size_t angle_end(std::string_view line, std::size_t i) {
    if (i >= line.size() || line[i] != '<') return std::string_view::npos;
    auto stop = [](char c) { return c == '<' || c == '>' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}'; };
    ++i;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '>') return i + 1;
        if (c == '<') {
            ++i;
            while (i < line.size() && !stop(line[i])) ++i;
            if (i >= line.size() || line[i] != '>') return std::string_view::npos;
            ++i;
            continue;
        }
        if (stop(c)) return std::string_view::npos;
        ++i;
    }
    return std::string_view::npos;
}

std::size_t walnut_number_end(std::string_view line, std::size_t i) {
    const std::size_t n = line.size();
    auto letters = [&](std::size_t j) {
        while (j < n && ascii_ident_char(line[j])) ++j;
        return j;
    };
    if (line[i] == '0' && i + 2 < n) {
        const char p = line[i + 1];
        if ((p == 'x' || p == 'X') && hex_digit(line[i + 2])) return letters(i + 2);
        if ((p == 'b' || p == 'B') && (line[i + 2] == '0' || line[i + 2] == '1')) return letters(i + 2);
        if ((p == 'o' || p == 'O') && line[i + 2] >= '0' && line[i + 2] <= '7') return letters(i + 2);
    }
    std::size_t j = i;
    while (j < n && (digit(line[j]) || line[j] == '_' || line[j] == '\'')) ++j;
    if (j + 1 < n && line[j] == '.' && digit(line[j + 1])) {
        ++j;
        while (j < n && (digit(line[j]) || line[j] == '_' || line[j] == '\'')) ++j;
    }
    if (j < n && (line[j] == 'e' || line[j] == 'E')) {
        std::size_t k = j + 1;
        if (k < n && (line[k] == '+' || line[k] == '-')) ++k;
        if (k < n && digit(line[k])) {
            while (k < n && digit(line[k])) ++k;
            j = k;
        }
    }
    return letters(j);
}

std::size_t walnut_escape_end(std::string_view line, std::size_t i, std::size_t limit) {
    if (i + 1 >= limit) return limit;
    const char n = line[i + 1];
    if (n == 'u' && i + 2 < limit && line[i + 2] == '{') {
        std::size_t j = i + 3;
        while (j < limit && hex_digit(line[j])) ++j;
        if (j > i + 3 && j < limit && line[j] == '}') return j + 1;
    }
    if (n == 'x' && i + 3 < limit && hex_digit(line[i + 2]) && hex_digit(line[i + 3])) return i + 4;
    if (n >= '0' && n <= '7') {
        std::size_t j = i + 2;
        while (j < limit && j < i + 4 && line[j] >= '0' && line[j] <= '7') ++j;
        return j;
    }
    std::size_t j = i + 2;
    while (j < limit && (static_cast<unsigned char>(line[j]) & 0xC0) == 0x80) ++j;
    return j;
}

std::size_t walnut_block_comment(std::string_view line, std::size_t i, int& depth) {
    while (i < line.size()) {
        if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            ++depth;
            i += 2;
            continue;
        }
        if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
            --depth;
            i += 2;
            if (depth == 0) return i;
            continue;
        }
        ++i;
    }
    return line.size();
}

std::size_t walnut_quoted(std::vector<CodeToken>& out, std::string_view line, std::size_t i, char quote) {
    std::size_t run = i;
    ++i;
    while (i < line.size()) {
        if (line[i] == '\\') {
            const std::size_t e = walnut_escape_end(line, i, line.size());
            push(out, run, i, CodeTokenKind::String);
            push(out, i, e, CodeTokenKind::Escape);
            run = i = e;
            continue;
        }
        if (line[i] == quote) {
            push(out, run, i + 1, CodeTokenKind::String);
            return i + 1;
        }
        ++i;
    }
    push(out, run, line.size(), CodeTokenKind::String);
    return line.size();
}

std::vector<CodeToken> highlight_walnut(std::string_view line, CodeLineState& state);

std::size_t walnut_interpolation_end(std::string_view line, std::size_t i) {
    int depth = 1;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '"' || c == '\'') {
            i = skip_string(line, i, c);
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
        ++i;
    }
    return std::string_view::npos;
}

std::size_t walnut_template(std::vector<CodeToken>& out, std::string_view line, std::size_t i, CodeLineState& state) {
    std::size_t run = i;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '\\') {
            const std::size_t e = walnut_escape_end(line, i, line.size());
            push(out, run, i, CodeTokenKind::String);
            push(out, i, e, CodeTokenKind::Escape);
            run = i = e;
            continue;
        }
        if (c == '`') {
            push(out, run, i + 1, CodeTokenKind::String);
            state.string_open = false;
            state.raw_delimiter.clear();
            return i + 1;
        }
        if (c == '$' && i + 1 < line.size() && line[i + 1] == '{') {
            const std::size_t close = walnut_interpolation_end(line, i + 2);
            if (close != std::string_view::npos) {
                push(out, run, i + 2, CodeTokenKind::String);
                CodeLineState inner;
                for (const CodeToken& t : highlight_walnut(line.substr(i + 2, close - i - 2), inner)) push(out, t.begin + i + 2, t.end + i + 2, t.kind);
                run = close;
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
    push(out, run, line.size(), CodeTokenKind::String);
    state.string_open = true;
    state.raw_delimiter = "`";
    return line.size();
}

std::size_t walnut_operator_end(std::string_view line, std::size_t i) {
    const std::size_t n = line.size();
    auto at = [&](std::size_t k) { return k < n ? line[k] : '\0'; };
    const char c = line[i];
    const char d = at(i + 1);
    if (c == '*' && d == '*') return at(i + 2) == '=' ? i + 3 : i + 2;
    if ((c == '<' && d == '<') || (c == '>' && d == '>')) return at(i + 2) == '=' ? i + 3 : i + 2;
    if ((c == '<' || c == '>' || c == '=' || c == '!') && d == '=') return i + 2;
    if ((c == '&' && d == '&') || (c == '|' && d == '|') || (c == '+' && d == '+') || (c == '-' && d == '-')) return i + 2;
    if (std::string_view("-+*/%&|^~!=<>?").find(c) != std::string_view::npos) return d == '=' ? i + 2 : i + 1;
    return std::string_view::npos;
}

std::vector<CodeToken> highlight_walnut(std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    const std::size_t n = line.size();
    std::size_t i = 0;
    if (state.comment_depth > 0) {
        i = walnut_block_comment(line, 0, state.comment_depth);
        push(out, 0, i, CodeTokenKind::Comment);
        if (state.comment_depth > 0) return out;
    }
    if (state.string_open && state.raw_delimiter == "`") {
        i = walnut_template(out, line, i, state);
        if (state.string_open) return out;
    }
    const std::size_t first = skip_spaces(line, 0);
    if (i <= first && first < n && line[first] == '#') {
        const std::size_t name = skip_spaces(line, first + 1);
        const std::size_t end = name < n && ascii_ident_start(line[name]) ? word_end(line, name) : name;
        if (end > name) {
            push(out, first, end, CodeTokenKind::Preprocessor);
            i = end;
        }
    }
    auto spaced_word = [&](std::size_t from, std::size_t& begin, std::size_t& end, bool dots) {
        std::size_t j = from;
        while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
        if (j == from || j >= n || !ascii_ident_start(line[j])) return false;
        begin = j;
        while (j < n && (ascii_ident_char(line[j]) || (dots && line[j] == '.'))) ++j;
        end = j;
        return true;
    };
    while (i < n) {
        const char c = line[i];
        const bool boundary = i == 0 || !ascii_ident_char(line[i - 1]);
        if (c == '/' && i + 1 < n && line[i + 1] == '/') {
            push(out, i, n, CodeTokenKind::Comment);
            break;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '*') {
            int depth = 1;
            const std::size_t end = walnut_block_comment(line, i + 2, depth);
            push(out, i, end, CodeTokenKind::Comment);
            if (depth > 0) {
                state.comment_depth = depth;
                break;
            }
            i = end;
            continue;
        }
        if (c == '"' || c == '\'') {
            i = walnut_quoted(out, line, i, c);
            continue;
        }
        if (c == '`') {
            push(out, i, i + 1, CodeTokenKind::String);
            i = walnut_template(out, line, i + 1, state);
            if (state.string_open) break;
            continue;
        }
        if (digit(c) && boundary) {
            const std::size_t end = walnut_number_end(line, i);
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (ascii_ident_start(c) && boundary) {
            const std::size_t end = word_end(line, i);
            const std::string word(line.substr(i, end - i));
            std::size_t b = 0;
            std::size_t e = 0;
            if (word == "end" && spaced_word(end, b, e, false)) {
                const std::string what(line.substr(b, e - b));
                const bool fn = what == "function";
                const bool type = what == "class" || what == "struct" || what == "union" || what == "enum" || what == "concept";
                const bool space = what == "namespace" || what == "module" || what == "extern";
                if (fn || type || space) {
                    push(out, i, end, CodeTokenKind::ControlKeyword);
                    push(out, end, b, CodeTokenKind::Plain);
                    push(out, b, e, CodeTokenKind::Keyword);
                    std::size_t nb = 0;
                    std::size_t ne = 0;
                    i = e;
                    if (spaced_word(e, nb, ne, space)) {
                        push(out, e, nb, CodeTokenKind::Plain);
                        push(out, nb, ne, fn ? CodeTokenKind::Function : type ? CodeTokenKind::Type : CodeTokenKind::Namespace);
                        i = ne;
                    }
                    continue;
                }
            }
            const bool type_word = word == "class" || word == "struct" || word == "union" || word == "enum" || word == "concept";
            const bool space_word = word == "namespace" || word == "module";
            if ((word == "function" || type_word || space_word) && spaced_word(end, b, e, space_word)) {
                push(out, i, end, CodeTokenKind::Keyword);
                push(out, end, b, CodeTokenKind::Plain);
                push(out, b, e, word == "function" ? CodeTokenKind::Function : type_word ? CodeTokenKind::Type : CodeTokenKind::Namespace);
                i = e;
                continue;
            }
            if (word == "operator") {
                const std::size_t open = skip_spaces(line, end);
                const std::size_t close = open < n && line[open] == '(' ? line.find(')', open) : std::string_view::npos;
                if (close != std::string_view::npos) {
                    push(out, i, end, CodeTokenKind::Keyword);
                    push(out, end, open, CodeTokenKind::Plain);
                    push(out, open, close + 1, CodeTokenKind::OverloadedOperator);
                    i = close + 1;
                    continue;
                }
            }
            CodeTokenKind kind = CodeTokenKind::Plain;
            bool known = true;
            if (word == "invalidate" && spaced_word(end, b, e, false)) kind = CodeTokenKind::ControlKeyword;
            else if (word == "self") kind = CodeTokenKind::SelfKeyword;
            else if (walnut_control().count(word)) kind = CodeTokenKind::ControlKeyword;
            else if (walnut_storage().count(word)) kind = CodeTokenKind::Keyword;
            else if (walnut_primitives().count(word)) kind = CodeTokenKind::BuiltinType;
            else if (word == "iterable" && skip_spaces(line, end) < n && line[skip_spaces(line, end)] == '<') kind = CodeTokenKind::BuiltinType;
            else if (walnut_constants().count(word)) kind = CodeTokenKind::Constant;
            else if (word == "this") kind = CodeTokenKind::ThisKeyword;
            else if (walnut_casts().count(word)) kind = CodeTokenKind::Cast;
            else if (walnut_word_operators().count(word)) kind = CodeTokenKind::BuiltinOperator;
            else known = false;
            if (!known) {
                const std::size_t after = skip_spaces(line, end);
                const std::size_t angles = angle_end(line, after);
                const std::size_t past = angles == std::string_view::npos ? std::string_view::npos : skip_spaces(line, angles);
                const bool scope_at = [&](std::size_t k) { return k != std::string_view::npos && k + 1 < n && line[k] == ':' && line[k + 1] == ':'; }(past);
                const bool call_after = (after < n && line[after] == '(') || (past != std::string_view::npos && past < n && line[past] == '(');
                if (scope_at) kind = CodeTokenKind::TemplateScope;
                else if (call_after) kind = CodeTokenKind::Function;
                else if (after + 1 < n && line[after] == ':' && line[after + 1] == ':') kind = CodeTokenKind::Namespace;
                else if (word[0] >= 'A' && word[0] <= 'Z') kind = CodeTokenKind::Type;
                else kind = CodeTokenKind::Variable;
            }
            push(out, i, end, kind);
            i = end;
            continue;
        }
        if (c == ':' && i > 0 && (i + 1 >= n || line[i + 1] != ':')) {
            const char before = line[i - 1];
            const bool after_name = ascii_ident_char(before) || before == '>' || before == '&' || before == '*' || before == ']' || before == ')';
            const std::size_t name = skip_spaces(line, i + 1);
            if (after_name && name < n && ascii_ident_start(line[name])) {
                const std::size_t end = word_end(line, name);
                const std::string word(line.substr(name, end - name));
                static const std::set<std::string> blocked = {
                    "return", "break", "continue", "then", "else", "if", "for", "while", "do", "switch", "case", "default", "throw", "try",
                    "catch", "repeat", "fallthrough", "in", "is", "as", "end", "co_return", "co_yield", "await",
                };
                const std::size_t after = skip_spaces(line, end);
                if (!blocked.count(word) && (after >= n || line[after] != '(')) {
                    push(out, i, name, CodeTokenKind::Plain);
                    push(out, name, end, CodeTokenKind::Declaration);
                    i = end;
                    continue;
                }
            }
        }
        if (c == '-' && i + 1 < n && line[i + 1] == '>') {
            push(out, i, i + 2, CodeTokenKind::BuiltinOperator);
            i += 2;
            continue;
        }
        if (c == ':' && i + 1 < n && line[i + 1] == ':') {
            push(out, i, i + 2, CodeTokenKind::Plain);
            i += 2;
            continue;
        }
        const std::size_t op = walnut_operator_end(line, i);
        if (op != std::string_view::npos) {
            push(out, i, op, CodeTokenKind::BuiltinOperator);
            i = op;
            continue;
        }
        std::size_t next = i + 1;
        if (static_cast<unsigned char>(c) >= 0x80) {
            while (next < n && (static_cast<unsigned char>(line[next]) & 0xC0) == 0x80) ++next;
        } else if (ascii_ident_char(c)) {
            next = word_end(line, i);
        }
        push(out, i, next, CodeTokenKind::Plain);
        i = next;
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

const std::set<std::string>& cmake_control() {
    static const std::set<std::string> k = {
        "if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile", "function", "endfunction", "macro", "endmacro",
        "return", "break", "continue", "block", "endblock",
    };
    return k;
}

const std::set<std::string>& cmake_constants() {
    static const std::set<std::string> k = { "ON", "OFF", "TRUE", "FALSE", "YES", "NO", "Y", "N", "IGNORE", "NOTFOUND" };
    return k;
}

const std::set<std::string>& cmake_operators() {
    static const std::set<std::string> k = {
        "AND", "OR", "NOT", "COMMAND", "POLICY", "TARGET", "TEST", "DEFINED", "EXISTS", "IS_DIRECTORY", "IS_ABSOLUTE", "IS_SYMLINK",
        "IS_NEWER_THAN", "IN_LIST", "MATCHES", "LESS", "GREATER", "EQUAL", "LESS_EQUAL", "GREATER_EQUAL", "STRLESS", "STRGREATER",
        "STREQUAL", "STRLESS_EQUAL", "STRGREATER_EQUAL", "VERSION_LESS", "VERSION_GREATER", "VERSION_EQUAL", "VERSION_LESS_EQUAL",
        "VERSION_GREATER_EQUAL", "PATH_EQUAL",
    };
    return k;
}

std::size_t cmake_bracket_open(std::string_view line, std::size_t i, int& level) {
    if (i >= line.size() || line[i] != '[') return std::string_view::npos;
    std::size_t j = i + 1;
    int equals = 0;
    while (j < line.size() && line[j] == '=') {
        ++equals;
        ++j;
    }
    if (j < line.size() && line[j] == '[') {
        level = equals;
        return j + 1;
    }
    return std::string_view::npos;
}

std::size_t cmake_bracket_close(std::string_view line, std::size_t from, int level) {
    const std::string close = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
    const std::size_t at = line.find(close, from);
    return at == std::string_view::npos ? at : at + close.size();
}

std::size_t cmake_reference_end(std::string_view line, std::size_t i, std::size_t limit) {
    if (i + 1 >= limit || line[i] != '$') return i;
    std::size_t j = i + 1;
    char open = line[j];
    if (open != '{' && open != '<') {
        while (j < limit && std::isalpha(static_cast<unsigned char>(line[j]))) ++j;
        if (j >= limit || line[j] != '{' || j == i + 1) return i;
        open = '{';
    }
    const char close = open == '{' ? '}' : '>';
    int depth = 0;
    for (; j < limit; ++j) {
        if (line[j] == open) ++depth;
        else if (line[j] == close && --depth == 0) return j + 1;
    }
    return limit;
}

void push_cmake_text(std::vector<CodeToken>& out, std::string_view line, std::size_t begin, std::size_t end, CodeTokenKind base) {
    std::size_t run = begin;
    std::size_t i = begin;
    while (i < end) {
        if (line[i] == '\\' && i + 1 < end) {
            push(out, run, i, base);
            push(out, i, i + 2, CodeTokenKind::Escape);
            i += 2;
            run = i;
            continue;
        }
        const std::size_t ref = cmake_reference_end(line, i, end);
        if (ref > i) {
            push(out, run, i, base);
            push(out, i, ref, CodeTokenKind::Variable);
            i = ref;
            run = i;
            continue;
        }
        ++i;
    }
    push(out, run, end, base);
}

std::size_t cmake_string_end(std::string_view line, std::size_t from) {
    for (std::size_t j = from; j < line.size(); ++j) {
        if (line[j] == '\\') {
            ++j;
            continue;
        }
        if (line[j] == '"') return j + 1;
    }
    return std::string_view::npos;
}

std::vector<CodeToken> highlight_cmake(std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    std::size_t i = 0;
    if (state.bracket_level >= 0) {
        const CodeTokenKind kind = state.bracket_comment ? CodeTokenKind::Comment : CodeTokenKind::String;
        const std::size_t close = cmake_bracket_close(line, 0, state.bracket_level);
        if (close == std::string_view::npos) {
            push(out, 0, line.size(), kind);
            return out;
        }
        push(out, 0, close, kind);
        state.bracket_level = -1;
        state.bracket_comment = false;
        i = close;
    }
    if (state.string_open) {
        const std::size_t close = cmake_string_end(line, i);
        if (close == std::string_view::npos) {
            push_cmake_text(out, line, i, line.size(), CodeTokenKind::String);
            return out;
        }
        push_cmake_text(out, line, i, close, CodeTokenKind::String);
        state.string_open = false;
        i = close;
    }
    while (i < line.size()) {
        const char c = line[i];
        if (c == '#') {
            int level = 0;
            const std::size_t open = cmake_bracket_open(line, i + 1, level);
            if (open != std::string_view::npos) {
                const std::size_t close = cmake_bracket_close(line, open, level);
                if (close == std::string_view::npos) {
                    push(out, i, line.size(), CodeTokenKind::Comment);
                    state.bracket_level = level;
                    state.bracket_comment = true;
                    break;
                }
                push(out, i, close, CodeTokenKind::Comment);
                i = close;
                continue;
            }
            push(out, i, line.size(), CodeTokenKind::Comment);
            break;
        }
        if (c == '[') {
            int level = 0;
            const std::size_t open = cmake_bracket_open(line, i, level);
            if (open != std::string_view::npos) {
                const std::size_t close = cmake_bracket_close(line, open, level);
                if (close == std::string_view::npos) {
                    push(out, i, line.size(), CodeTokenKind::String);
                    state.bracket_level = level;
                    state.bracket_comment = false;
                    break;
                }
                push(out, i, close, CodeTokenKind::String);
                i = close;
                continue;
            }
        }
        if (c == '"') {
            const std::size_t close = cmake_string_end(line, i + 1);
            if (close == std::string_view::npos) {
                push_cmake_text(out, line, i, line.size(), CodeTokenKind::String);
                state.string_open = true;
                break;
            }
            push_cmake_text(out, line, i, close, CodeTokenKind::String);
            i = close;
            continue;
        }
        if (c == '$' || c == '\\') {
            const std::size_t ref = c == '$' ? cmake_reference_end(line, i, line.size()) : std::min(line.size(), i + 2);
            if (ref > i) {
                push(out, i, ref, c == '$' ? CodeTokenKind::Variable : CodeTokenKind::Escape);
                i = ref;
                continue;
            }
        }
        if (digit(c) && (i == 0 || !ident_char(line[i - 1]))) {
            std::size_t end = i;
            while (end < line.size() && (digit(line[end]) || line[end] == '.')) ++end;
            if (end >= line.size() || !ident_char(line[end])) {
                push(out, i, end, CodeTokenKind::Number);
                i = end;
                continue;
            }
        }
        if (ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string word(line.substr(i, end - i));
            const std::size_t next = skip_spaces(line, end);
            const bool call = next < line.size() && line[next] == '(';
            const bool loose = i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t' || line[i - 1] == '(';
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (!loose) kind = CodeTokenKind::Plain;
            else if (cmake_operators().count(word)) kind = CodeTokenKind::Keyword;
            else if (call) kind = cmake_control().count(lower(word)) ? CodeTokenKind::Keyword : CodeTokenKind::Function;
            else if (cmake_constants().count(word)) kind = CodeTokenKind::Constant;
            else if (all_upper(word) && (word.rfind("CMAKE_", 0) == 0 || word.rfind("PROJECT_", 0) == 0)) kind = CodeTokenKind::Variable;
            else if (all_upper(word)) kind = CodeTokenKind::Attribute;
            push(out, i, end, kind);
            i = end;
            continue;
        }
        push(out, i, i + 1, CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

const std::set<std::string>& js_control() {
    static const std::set<std::string> k = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "throw", "try", "catch", "finally", "await", "yield",
    };
    return k;
}

const std::set<std::string>& js_keywords() {
    static const std::set<std::string> k = {
        "var", "let", "const", "function", "class", "extends", "new", "delete", "typeof", "instanceof", "in", "of", "import", "export", "from", "as",
        "async", "static", "get", "set", "super", "void", "with", "debugger",
    };
    return k;
}

const std::set<std::string>& js_constants() {
    static const std::set<std::string> k = { "true", "false", "null", "undefined", "NaN", "Infinity" };
    return k;
}

const std::set<std::string>& js_globals() {
    static const std::set<std::string> k = {
        "console", "document", "window", "globalThis", "navigator", "location", "history", "localStorage", "sessionStorage", "Math", "JSON", "Date",
        "Promise", "Array", "Object", "String", "Number", "Boolean", "Symbol", "BigInt", "Map", "Set", "WeakMap", "WeakSet", "RegExp", "Error",
        "TypeError", "RangeError", "SyntaxError", "Intl", "Reflect", "Proxy", "fetch", "setTimeout", "setInterval", "clearTimeout", "clearInterval",
        "requestAnimationFrame", "require", "module", "exports", "process", "Buffer", "__dirname", "__filename", "$", "jQuery", "alert", "parseInt",
        "parseFloat", "isNaN", "isFinite", "encodeURIComponent", "decodeURIComponent", "structuredClone", "queueMicrotask",
    };
    return k;
}

bool js_ident_start(char c) noexcept { return ident_start(c) || c == '$'; }

bool js_ident_char(char c) noexcept { return ident_char(c) || c == '$'; }

bool js_regex_allowed(const std::vector<CodeToken>& out, std::string_view line) {
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        if (it->kind == CodeTokenKind::Comment) continue;
        const std::string_view text = line.substr(it->begin, it->end - it->begin);
        if (text.find_first_not_of(" \t") == std::string_view::npos) continue;
        if (it->kind == CodeTokenKind::Operator) {
            const char last = text.back();
            return last != ')' && last != ']' && last != '}';
        }
        if (it->kind == CodeTokenKind::ControlKeyword) return true;
        if (it->kind == CodeTokenKind::Keyword) return text != "this" && text != "super";
        return false;
    }
    return true;
}

std::size_t js_regex_end(std::string_view line, std::size_t i) {
    bool in_class = false;
    std::size_t j = i + 1;
    while (j < line.size()) {
        const char c = line[j];
        if (c == '\\') {
            j += 2;
            continue;
        }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        else if (c == '/' && !in_class) {
            ++j;
            while (j < line.size() && std::isalpha(static_cast<unsigned char>(line[j]))) ++j;
            return j;
        }
        ++j;
    }
    return std::string_view::npos;
}

std::vector<CodeToken> highlight_javascript(std::string_view line, CodeLineState& state);

std::size_t js_template(std::vector<CodeToken>& out, std::string_view line, std::size_t i, CodeLineState& state) {
    std::size_t run = i;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            push(out, run, i, CodeTokenKind::String);
            push(out, i, i + 2, CodeTokenKind::Escape);
            i += 2;
            run = i;
            continue;
        }
        if (c == '`') {
            push(out, run, i + 1, CodeTokenKind::String);
            state.string_open = false;
            return i + 1;
        }
        if (c == '$' && i + 1 < line.size() && line[i + 1] == '{') {
            push(out, run, i, CodeTokenKind::String);
            push(out, i, i + 2, CodeTokenKind::Operator);
            int depth = 1;
            std::size_t j = i + 2;
            while (j < line.size() && depth > 0) {
                if (line[j] == '{') ++depth;
                else if (line[j] == '}' && --depth == 0) break;
                ++j;
            }
            CodeLineState inner;
            for (CodeToken t : highlight_javascript(line.substr(i + 2, j - (i + 2)), inner)) {
                t.begin += i + 2;
                t.end += i + 2;
                out.push_back(t);
            }
            if (j < line.size()) push(out, j, j + 1, CodeTokenKind::Operator);
            i = std::min(line.size(), j + 1);
            run = i;
            continue;
        }
        ++i;
    }
    push(out, run, line.size(), CodeTokenKind::String);
    state.string_open = true;
    return line.size();
}

std::vector<CodeToken> highlight_javascript(std::string_view line, CodeLineState& state) {
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
    if (state.string_open) i = js_template(out, line, i, state);
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
        if (c == '/' && js_regex_allowed(out, line)) {
            const std::size_t end = js_regex_end(line, i);
            if (end != std::string_view::npos) {
                push(out, i, end, CodeTokenKind::String);
                i = end;
                continue;
            }
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = skip_string(line, i, c);
            push_quoted(out, line, i, end);
            i = end;
            continue;
        }
        if (c == '`') {
            push(out, i, i + 1, CodeTokenKind::String);
            i = js_template(out, line, i + 1, state);
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '.' || ((line[end] == '+' || line[end] == '-') && (line[end - 1] == 'e' || line[end - 1] == 'E') && !(line[i] == '0' && i + 1 < line.size() && (line[i + 1] == 'x' || line[i + 1] == 'X'))))) ++end;
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (js_ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && js_ident_char(line[end])) ++end;
            const std::string word(line.substr(i, end - i));
            std::size_t next = end;
            while (next < line.size() && (line[next] == ' ' || line[next] == '\t')) ++next;
            std::size_t prev = i;
            while (prev > 0 && (line[prev - 1] == ' ' || line[prev - 1] == '\t')) --prev;
            const bool member = prev > 0 && line[prev - 1] == '.' && !(prev > 1 && line[prev - 2] == '.');
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (!member && js_control().count(word)) kind = CodeTokenKind::ControlKeyword;
            else if (!member && (word == "this")) kind = CodeTokenKind::ThisKeyword;
            else if (!member && js_keywords().count(word)) kind = CodeTokenKind::Keyword;
            else if (!member && js_constants().count(word)) kind = CodeTokenKind::Constant;
            else if (next < line.size() && line[next] == '(') kind = CodeTokenKind::Function;
            else if (member) kind = CodeTokenKind::Property;
            else if (js_globals().count(word)) kind = CodeTokenKind::BuiltinType;
            else if (std::isupper(static_cast<unsigned char>(word[0])) && !all_upper(word)) kind = CodeTokenKind::Type;
            else if (all_upper(word)) kind = CodeTokenKind::Constant;
            else if (next < line.size() && line[next] == ':' && !(next + 1 < line.size() && line[next + 1] == ':')) kind = CodeTokenKind::Property;
            push(out, i, end, kind);
            i = end;
            continue;
        }
        if (c == '#' && i + 1 < line.size() && js_ident_start(line[i + 1])) {
            std::size_t end = i + 1;
            while (end < line.size() && js_ident_char(line[end])) ++end;
            push(out, i, end, CodeTokenKind::Property);
            i = end;
            continue;
        }
        if (c == ' ' || c == '\t') {
            std::size_t end = i;
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
            push(out, i, end, CodeTokenKind::Plain);
            i = end;
            continue;
        }
        push(out, i, i + 1, std::ispunct(static_cast<unsigned char>(c)) && c != ';' && c != ',' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

std::vector<CodeToken> highlight_json(std::string_view line, CodeLineState& state) {
    (void)state;
    std::vector<CodeToken> out;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '"') {
            const std::size_t end = skip_string(line, i, c);
            std::size_t next = end;
            while (next < line.size() && (line[next] == ' ' || line[next] == '\t')) ++next;
            if (next < line.size() && line[next] == ':') push(out, i, end, CodeTokenKind::Property);
            else push_quoted(out, line, i, end);
            i = end;
            continue;
        }
        if (digit(c) || (c == '-' && i + 1 < line.size() && digit(line[i + 1]))) {
            std::size_t end = i + 1;
            while (end < line.size() && (digit(line[end]) || line[end] == '.' || line[end] == 'e' || line[end] == 'E' || line[end] == '+' || line[end] == '-')) ++end;
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string_view word = line.substr(i, end - i);
            push(out, i, end, word == "true" || word == "false" || word == "null" ? CodeTokenKind::Constant : CodeTokenKind::Plain);
            i = end;
            continue;
        }
        push(out, i, i + 1, c == '{' || c == '}' || c == '[' || c == ']' || c == ':' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

std::vector<CodeToken> highlight_css(std::string_view line, CodeLineState& state) {
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
    int& depth = state.comment_depth;
    bool value = false;
    bool selector = true;
    std::size_t decided = static_cast<std::size_t>(-1);
    auto decide = [&](std::size_t from) {
        std::size_t j = from;
        char quote = '\0';
        while (j < line.size()) {
            const char d = line[j];
            if (quote) {
                if (d == '\\') ++j;
                else if (d == quote) quote = '\0';
            } else if (d == '"' || d == '\'') {
                quote = d;
            } else if (d == '/' && j + 1 < line.size() && line[j + 1] == '*') {
                const std::size_t close = line.find("*/", j + 2);
                if (close == std::string_view::npos) break;
                j = close + 1;
            } else if (d == '{') {
                selector = true;
                return;
            } else if (d == ';' || d == '}') {
                break;
            }
            ++j;
        }
        selector = depth <= 0;
    };
    while (i < line.size()) {
        if (decided != i && (decided == static_cast<std::size_t>(-1) || i == 0)) {
            decide(i);
            decided = i;
        }
        const char c = line[i];
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
        if (c == '{' || c == '}' || c == ';') {
            if (c == '{') ++depth;
            if (c == '}' && depth > 0) --depth;
            push(out, i, i + 1, CodeTokenKind::Plain);
            ++i;
            value = false;
            decide(i);
            decided = i;
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = skip_string(line, i, c);
            push(out, i, end, CodeTokenKind::String);
            i = end;
            continue;
        }
        if (c == '@') {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
            push(out, i, end, CodeTokenKind::ControlKeyword);
            i = end;
            continue;
        }
        if (c == '!' && line.compare(i, 10, "!important") == 0) {
            push(out, i, i + 10, CodeTokenKind::ControlKeyword);
            i += 10;
            continue;
        }
        if (selector) {
            if (c == '.' || c == '#') {
                std::size_t end = i + 1;
                while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
                push(out, i, end, CodeTokenKind::Attribute);
                i = end;
                continue;
            }
            if (c == ':') {
                std::size_t end = i + 1;
                if (end < line.size() && line[end] == ':') ++end;
                while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
                push(out, i, end, CodeTokenKind::Keyword);
                i = end;
                continue;
            }
            if (ident_start(c) || c == '*') {
                std::size_t end = i + 1;
                while (end < line.size() && (ident_char(line[end]) || line[end] == '-')) ++end;
                push(out, i, end, CodeTokenKind::Tag);
                i = end;
                continue;
            }
            push(out, i, i + 1, c == '>' || c == '+' || c == '~' || c == ',' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
            ++i;
            continue;
        }
        if (c == ':' && !value) {
            push(out, i, i + 1, CodeTokenKind::Operator);
            value = true;
            ++i;
            continue;
        }
        if (c == '#' && value) {
            std::size_t end = i + 1;
            while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end]))) ++end;
            push(out, i, end, CodeTokenKind::Constant);
            i = end;
            continue;
        }
        if (digit(c) || ((c == '-' || c == '.' || c == '+') && i + 1 < line.size() && (digit(line[i + 1]) || line[i + 1] == '.'))) {
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
            CodeTokenKind kind = CodeTokenKind::Keyword;
            if (!value) kind = line.compare(i, 2, "--") == 0 ? CodeTokenKind::Variable : CodeTokenKind::Property;
            else if (end < line.size() && line[end] == '(') kind = CodeTokenKind::Function;
            else if (line.compare(i, 2, "--") == 0) kind = CodeTokenKind::Variable;
            push(out, i, end, kind);
            i = end;
            continue;
        }
        push(out, i, i + 1, std::ispunct(static_cast<unsigned char>(c)) && c != ',' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

std::size_t find_closing_tag(std::string_view line, std::size_t from, std::string_view tag) {
    std::size_t at = from;
    while ((at = line.find("</", at)) != std::string_view::npos) {
        if (at + 2 + tag.size() <= line.size() && lower(line.substr(at + 2, tag.size())) == tag) return at;
        at += 2;
    }
    return std::string_view::npos;
}

void push_embedded(std::vector<CodeToken>& out, std::vector<CodeToken> tokens, std::size_t offset) {
    for (CodeToken t : tokens) {
        t.begin += offset;
        t.end += offset;
        out.push_back(t);
    }
}

std::vector<CodeToken> highlight_html(std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (state.embedded != 0) {
            const std::string_view tag = state.embedded == 1 ? "script" : "style";
            const std::size_t close = find_closing_tag(line, i, tag);
            const std::size_t stop = close == std::string_view::npos ? line.size() : close;
            const std::string_view piece = line.substr(i, stop - i);
            if (state.embedded == 1) push_embedded(out, highlight_javascript(piece, state), i);
            else push_embedded(out, highlight_css(piece, state), i);
            i = stop;
            if (close == std::string_view::npos) return out;
            state.embedded = 0;
            state.block_comment = false;
            state.string_open = false;
            state.comment_depth = 0;
            continue;
        }
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
        if (c == '<' && i + 1 < line.size() && (std::isalpha(static_cast<unsigned char>(line[i + 1])) || line[i + 1] == '/' || line[i + 1] == '!')) {
            std::size_t j = i + 1;
            const bool closing = line[j] == '/';
            if (line[j] == '/' || line[j] == '!') ++j;
            push(out, i, j, CodeTokenKind::Operator);
            std::size_t end = j;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '-' || line[end] == ':')) ++end;
            push(out, j, end, CodeTokenKind::Tag);
            const std::string name = lower(line.substr(j, end - j));
            i = end;
            bool self_closing = false;
            while (i < line.size() && line[i] != '>') {
                const char a = line[i];
                if (a == '"' || a == '\'') {
                    const std::size_t e = skip_string(line, i, a);
                    push(out, i, e, CodeTokenKind::String);
                    i = e;
                } else if (ident_start(a) || a == '@' || a == ':') {
                    std::size_t e = i;
                    while (e < line.size() && (ident_char(line[e]) || line[e] == '-' || line[e] == ':' || line[e] == '@' || line[e] == '.')) ++e;
                    push(out, i, e, CodeTokenKind::Attribute);
                    i = e;
                } else {
                    self_closing = a == '/';
                    push(out, i, i + 1, a == '=' || a == '/' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
                    ++i;
                }
            }
            if (i < line.size()) {
                push(out, i, i + 1, CodeTokenKind::Operator);
                ++i;
                if (!closing && !self_closing && name == "script") state.embedded = 1;
                if (!closing && !self_closing && name == "style") state.embedded = 2;
            }
            continue;
        }
        if (c == '&') {
            const std::size_t semi = line.find(';', i);
            if (semi != std::string_view::npos && semi - i <= 32 && semi > i + 1) {
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


struct Syntax {
    std::vector<std::string_view> line_comments;
    std::string_view block_open;
    std::string_view block_close;
    bool nested_blocks = false;
    std::set<std::string> control;
    std::set<std::string> keywords;
    std::set<std::string> types;
    std::set<std::string> constants;
    std::set<std::string> self_words;
    std::set<std::string> builtins;
    bool chars = false;
    bool lifetimes = false;
    bool rust_strings = false;
    bool rust_macros = false;
    bool rust_attributes = false;
    bool annotations = false;
    bool zig_builtins = false;
    bool zig_multiline = false;
    bool triple_strings = false;
    bool python_prefixes = false;
    bool lua_long = false;
    bool csharp_strings = false;
    bool hash_directives = false;
    bool def_names = false;
};

const Syntax& rust_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "//" };
        x.block_open = "/*";
        x.block_close = "*/";
        x.nested_blocks = true;
        x.control = { "if", "else", "match", "loop", "while", "for", "in", "break", "continue", "return", "yield", "await" };
        x.keywords = { "as", "async", "const", "crate", "dyn", "enum", "extern", "fn", "impl", "let", "mod", "move", "mut", "pub", "ref", "static", "struct",
                       "trait", "type", "union", "unsafe", "use", "where", "macro_rules" };
        x.types = { "i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64", "bool", "char", "str", "String",
                    "Vec", "Option", "Result", "Box", "Rc", "Arc", "RefCell", "Cell", "HashMap", "HashSet", "BTreeMap", "BTreeSet", "VecDeque" };
        x.constants = { "true", "false", "None", "Some", "Ok", "Err" };
        x.self_words = { "self", "Self", "super" };
        x.chars = true;
        x.lifetimes = true;
        x.rust_strings = true;
        x.rust_macros = true;
        x.rust_attributes = true;
        return x;
    }();
    return s;
}

const Syntax& zig_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "//" };
        x.control = { "if", "else", "switch", "while", "for", "break", "continue", "return", "defer", "errdefer", "try", "catch", "orelse", "unreachable",
                      "suspend", "resume", "nosuspend" };
        x.keywords = { "const", "var", "fn", "pub", "struct", "enum", "union", "error", "opaque", "test", "comptime", "inline", "noinline", "export", "extern",
                       "packed", "align", "callconv", "threadlocal", "usingnamespace", "and", "or", "anytype", "asm", "volatile", "allowzero", "linksection",
                       "noalias", "addrspace" };
        x.types = { "i8", "i16", "i32", "i64", "i128", "isize", "u1", "u8", "u16", "u32", "u64", "u128", "usize", "f16", "f32", "f64", "f80", "f128", "bool",
                    "void", "noreturn", "type", "anyerror", "anyopaque", "anyframe", "comptime_int", "comptime_float", "c_int", "c_uint", "c_long", "c_ulong",
                    "c_char", "c_short", "c_ushort", "c_longlong", "c_ulonglong", "c_longdouble" };
        x.constants = { "true", "false", "null", "undefined" };
        x.chars = true;
        x.zig_builtins = true;
        x.zig_multiline = true;
        return x;
    }();
    return s;
}

const Syntax& python_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "#" };
        x.control = { "if", "elif", "else", "for", "while", "break", "continue", "return", "pass", "try", "except", "finally", "raise", "with", "yield",
                      "await", "match", "case", "assert" };
        x.keywords = { "def", "class", "lambda", "import", "from", "as", "global", "nonlocal", "del", "in", "is", "not", "and", "or", "async" };
        x.constants = { "True", "False", "None", "NotImplemented", "Ellipsis", "__name__", "__file__", "__main__" };
        x.self_words = { "self", "cls" };
        x.builtins = { "print", "len", "range", "input", "int", "float", "str", "bool", "list", "dict", "set", "tuple", "object", "isinstance",
                       "issubclass", "enumerate", "zip", "map", "filter", "sorted", "reversed", "sum", "min", "max", "abs", "round", "open", "super",
                       "property", "staticmethod", "classmethod", "repr", "hash", "id", "iter", "next", "any", "all", "format", "getattr", "setattr",
                       "hasattr", "delattr", "vars", "dir", "chr", "ord", "bin", "hex", "oct", "pow", "divmod", "callable", "exec", "eval", "bytes",
                       "bytearray", "frozenset", "complex", "slice", "type", "Exception", "BaseException", "ValueError", "TypeError", "KeyError",
                       "IndexError", "RuntimeError", "StopIteration", "AttributeError", "ImportError", "ModuleNotFoundError", "OSError",
                       "FileNotFoundError", "ZeroDivisionError", "NotImplementedError", "KeyboardInterrupt" };
        x.triple_strings = true;
        x.python_prefixes = true;
        x.annotations = true;
        x.def_names = true;
        return x;
    }();
    return s;
}

const Syntax& lua_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "--" };
        x.control = { "if", "then", "else", "elseif", "end", "for", "while", "do", "repeat", "until", "break", "return", "goto", "in" };
        x.keywords = { "local", "function", "and", "or", "not" };
        x.constants = { "true", "false", "nil" };
        x.self_words = { "self" };
        x.builtins = { "print", "type", "pairs", "ipairs", "tostring", "tonumber", "require", "pcall", "xpcall", "error", "assert", "select", "next",
                       "rawget", "rawset", "rawequal", "rawlen", "setmetatable", "getmetatable", "unpack", "load", "loadfile", "dofile",
                       "collectgarbage", "string", "table", "math", "io", "os", "coroutine", "utf8", "debug", "package", "_G", "_VERSION" };
        x.lua_long = true;
        x.def_names = true;
        return x;
    }();
    return s;
}

const Syntax& java_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "//" };
        x.block_open = "/*";
        x.block_close = "*/";
        x.control = { "if", "else", "switch", "case", "default", "for", "while", "do", "break", "continue", "return", "throw", "try", "catch", "finally", "yield" };
        x.keywords = { "abstract", "assert", "class", "enum", "extends", "final", "implements", "import", "instanceof", "interface", "native", "new", "package",
                       "private", "protected", "public", "static", "strictfp", "synchronized", "throws", "transient", "volatile", "var", "record",
                       "sealed", "permits", "module", "requires", "exports", "opens", "uses", "provides" };
        x.types = { "boolean", "byte", "char", "short", "int", "long", "float", "double", "void", "String", "Object", "Integer", "Long", "Double", "Float",
                    "Boolean", "Character", "Byte", "Short", "List", "ArrayList", "Map", "HashMap", "Set", "HashSet", "System", "Math", "Scanner" };
        x.constants = { "true", "false", "null" };
        x.self_words = { "this", "super" };
        x.chars = true;
        x.triple_strings = true;
        x.annotations = true;
        return x;
    }();
    return s;
}

const Syntax& csharp_syntax() {
    static const Syntax s = [] {
        Syntax x;
        x.line_comments = { "//" };
        x.block_open = "/*";
        x.block_close = "*/";
        x.control = { "if", "else", "switch", "case", "default", "for", "foreach", "while", "do", "break", "continue", "return", "throw", "try", "catch",
                      "finally", "goto", "yield", "await", "when" };
        x.keywords = { "abstract", "as", "async", "base", "checked", "class", "const", "delegate", "enum", "event", "explicit", "extern", "fixed", "implicit",
                       "in", "interface", "internal", "is", "lock", "namespace", "new", "operator", "out", "override", "params", "private", "protected",
                       "public", "readonly", "ref", "sealed", "sizeof", "stackalloc", "static", "struct", "typeof", "unchecked", "unsafe", "using",
                       "virtual", "volatile", "var", "get", "set", "init", "value", "record", "partial", "where", "global", "nameof", "dynamic",
                       "required", "file", "scoped", "not", "and", "or", "with" };
        x.types = { "bool", "byte", "sbyte", "char", "decimal", "double", "float", "int", "uint", "long", "ulong", "short", "ushort", "object", "string",
                    "void", "nint", "nuint", "String", "Console", "List", "Dictionary", "Task", "Math", "Exception" };
        x.constants = { "true", "false", "null" };
        x.self_words = { "this" };
        x.chars = true;
        x.csharp_strings = true;
        x.hash_directives = true;
        return x;
    }();
    return s;
}

bool starts_at(std::string_view line, std::size_t i, std::string_view what) {
    return !what.empty() && i + what.size() <= line.size() && line.compare(i, what.size(), what) == 0;
}

std::size_t lua_long_open(std::string_view line, std::size_t i) {
    if (i >= line.size() || line[i] != '[') return std::string_view::npos;
    std::size_t j = i + 1;
    while (j < line.size() && line[j] == '=') ++j;
    if (j < line.size() && line[j] == '[') return j - i - 1;
    return std::string_view::npos;
}

std::size_t close_multiline(std::vector<CodeToken>& out, std::string_view line, std::size_t i, CodeLineState& state, CodeTokenKind kind) {
    const std::string closer = state.raw_delimiter;
    std::size_t at = i;
    while (true) {
        const std::size_t found = closer.empty() ? std::string_view::npos : line.find(closer, at);
        if (found == std::string_view::npos) {
            push(out, i, line.size(), kind);
            return line.size();
        }
        if (kind == CodeTokenKind::String && !state.raw_string && found > 0 && line[found - 1] == '\\') {
            at = found + 1;
            continue;
        }
        push(out, i, found + closer.size(), kind);
        if (kind == CodeTokenKind::Comment) state.block_comment = false;
        else state.string_open = false;
        state.raw_delimiter.clear();
        state.raw_string = false;
        return found + closer.size();
    }
}

std::size_t generic_block_comment(std::vector<CodeToken>& out, std::string_view line, std::size_t i, std::size_t skip, const Syntax& syn, CodeLineState& state) {
    std::size_t j = i + skip;
    while (j < line.size()) {
        if (syn.nested_blocks && starts_at(line, j, syn.block_open)) {
            ++state.comment_depth;
            j += syn.block_open.size();
            continue;
        }
        if (starts_at(line, j, syn.block_close)) {
            j += syn.block_close.size();
            if (--state.comment_depth <= 0) {
                state.comment_depth = 0;
                state.block_comment = false;
                push(out, i, j, CodeTokenKind::Comment);
                return j;
            }
            continue;
        }
        ++j;
    }
    push(out, i, line.size(), CodeTokenKind::Comment);
    return line.size();
}

std::size_t open_multiline(std::vector<CodeToken>& out, std::string_view line, std::size_t begin, std::size_t body, const std::string& closer, bool raw, CodeLineState& state, CodeTokenKind kind) {
    if (kind == CodeTokenKind::Comment) state.block_comment = true;
    else state.string_open = true;
    state.raw_string = raw;
    state.raw_delimiter = closer;
    push(out, begin, body, kind);
    return close_multiline(out, line, body, state, kind);
}

std::vector<CodeToken> highlight_generic(const Syntax& syn, std::string_view line, CodeLineState& state) {
    std::vector<CodeToken> out;
    std::size_t i = 0;
    if (state.block_comment) {
        if (!state.raw_delimiter.empty()) i = close_multiline(out, line, 0, state, CodeTokenKind::Comment);
        else i = generic_block_comment(out, line, 0, 0, syn, state);
    } else if (state.string_open) {
        i = close_multiline(out, line, 0, state, CodeTokenKind::String);
    }
    if (syn.hash_directives && i == 0) {
        const std::size_t first = skip_spaces(line, 0);
        if (first < line.size() && line[first] == '#') {
            push(out, 0, line.size(), CodeTokenKind::Preprocessor);
            return out;
        }
    }
    if (syn.zig_multiline && i == 0) {
        const std::size_t first = skip_spaces(line, 0);
        if (starts_at(line, first, "\\\\")) {
            push(out, 0, first, CodeTokenKind::Plain);
            push(out, first, line.size(), CodeTokenKind::String);
            return out;
        }
    }
    bool after_def = false;
    while (i < line.size()) {
        const char c = line[i];
        bool handled = false;
        for (std::string_view m : syn.line_comments) {
            if (!starts_at(line, i, m)) continue;
            if (syn.lua_long) {
                const std::size_t level = lua_long_open(line, i + m.size());
                if (level != std::string_view::npos) {
                    i = open_multiline(out, line, i, i + m.size() + level + 2, "]" + std::string(level, '=') + "]", true, state, CodeTokenKind::Comment);
                    handled = true;
                    break;
                }
            }
            push(out, i, line.size(), CodeTokenKind::Comment);
            return out;
        }
        if (handled) continue;
        if (!syn.block_open.empty() && starts_at(line, i, syn.block_open)) {
            state.block_comment = true;
            state.comment_depth = 1;
            i = generic_block_comment(out, line, i, syn.block_open.size(), syn, state);
            continue;
        }
        if (syn.lua_long && c == '[') {
            const std::size_t level = lua_long_open(line, i);
            if (level != std::string_view::npos) {
                i = open_multiline(out, line, i, i + level + 2, "]" + std::string(level, '=') + "]", true, state, CodeTokenKind::String);
                continue;
            }
        }
        if (syn.rust_attributes && c == '#' && (starts_at(line, i, "#[") || starts_at(line, i, "#!["))) {
            int depth = 0;
            std::size_t j = i;
            while (j < line.size()) {
                if (line[j] == '[') ++depth;
                else if (line[j] == ']' && --depth == 0) {
                    ++j;
                    break;
                }
                ++j;
            }
            push(out, i, j, CodeTokenKind::Attribute);
            i = j;
            continue;
        }
        if (syn.zig_builtins && c == '@' && i + 1 < line.size() && line[i + 1] == '"') {
            const std::size_t end = skip_string(line, i + 1, '"');
            push(out, i, end, CodeTokenKind::Plain);
            i = end;
            continue;
        }
        if ((syn.annotations || syn.zig_builtins) && c == '@' && i + 1 < line.size() && ident_start(line[i + 1])) {
            std::size_t j = i + 1;
            while (j < line.size() && (ident_char(line[j]) || (syn.annotations && line[j] == '.'))) ++j;
            push(out, i, j, syn.zig_builtins ? CodeTokenKind::Function : CodeTokenKind::Attribute);
            i = j;
            continue;
        }
        const bool word_start = i == 0 || !ident_char(line[i - 1]);
        if (syn.rust_strings && word_start && (c == 'r' || c == 'b' || c == 'c')) {
            std::size_t j = i;
            if (line[j] == 'b' || line[j] == 'c') ++j;
            if (j < line.size() && line[j] == 'r') {
                std::size_t k = j + 1;
                while (k < line.size() && line[k] == '#') ++k;
                if (k < line.size() && line[k] == '"') {
                    i = open_multiline(out, line, i, k + 1, "\"" + std::string(k - j - 1, '#'), true, state, CodeTokenKind::String);
                    continue;
                }
            }
            if (j > i && j < line.size() && (line[j] == '"' || line[j] == '\'')) {
                const std::size_t end = skip_string(line, j, line[j]);
                push(out, i, j, CodeTokenKind::String);
                push_quoted(out, line, j, end);
                i = end;
                continue;
            }
        }
        if (syn.csharp_strings && (c == '@' || c == '$')) {
            std::size_t j = i;
            bool verbatim = false;
            while (j < line.size() && (line[j] == '@' || line[j] == '$') && j - i < 4) {
                if (line[j] == '@') verbatim = true;
                ++j;
            }
            if (j < line.size() && line[j] == '"') {
                if (starts_at(line, j, "\"\"\"")) {
                    std::size_t q = j;
                    while (q < line.size() && line[q] == '"') ++q;
                    i = open_multiline(out, line, i, q, std::string(q - j, '"'), true, state, CodeTokenKind::String);
                    continue;
                }
                if (verbatim) {
                    std::size_t k = j + 1;
                    while (k < line.size()) {
                        if (line[k] == '"') {
                            if (k + 1 < line.size() && line[k + 1] == '"') {
                                k += 2;
                                continue;
                            }
                            break;
                        }
                        ++k;
                    }
                    if (k >= line.size()) {
                        push(out, i, line.size(), CodeTokenKind::String);
                        state.string_open = true;
                        state.raw_string = true;
                        state.raw_delimiter = "\"";
                        return out;
                    }
                    push(out, i, k + 1, CodeTokenKind::String);
                    i = k + 1;
                    continue;
                }
                const std::size_t end = skip_string(line, j, '"');
                push(out, i, j, CodeTokenKind::String);
                push_quoted(out, line, j, end);
                i = end;
                continue;
            }
        }
        if (syn.python_prefixes && word_start && std::isalpha(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < line.size() && j - i < 2 && std::string_view("rRbBfFuU").find(line[j]) != std::string_view::npos) ++j;
            if (j > i && j < line.size() && (line[j] == '"' || line[j] == '\'')) {
                const bool raw = line.substr(i, j - i).find_first_of("rR") != std::string_view::npos;
                if (starts_at(line, j, "\"\"\"") || starts_at(line, j, "\'\'\'")) {
                    i = open_multiline(out, line, i, j + 3, std::string(line.substr(j, 3)), raw, state, CodeTokenKind::String);
                    continue;
                }
                const std::size_t end = skip_string(line, j, line[j]);
                push(out, i, j, CodeTokenKind::String);
                if (raw) push(out, j, end, CodeTokenKind::String);
                else push_quoted(out, line, j, end);
                i = end;
                continue;
            }
        }
        if (syn.triple_strings && (starts_at(line, i, "\"\"\"") || (syn.python_prefixes && starts_at(line, i, "\'\'\'")))) {
            i = open_multiline(out, line, i, i + 3, std::string(line.substr(i, 3)), false, state, CodeTokenKind::String);
            continue;
        }
        if (c == '"' || (c == '\'' && !syn.chars)) {
            const std::size_t end = skip_string(line, i, c);
            push_quoted(out, line, i, end);
            i = end;
            continue;
        }
        if (c == '\'' && syn.chars) {
            if (syn.lifetimes && i + 1 < line.size() && ident_start(line[i + 1])) {
                std::size_t j = i + 1;
                while (j < line.size() && ident_char(line[j])) ++j;
                if (j >= line.size() || line[j] != '\'') {
                    push(out, i, j, CodeTokenKind::Attribute);
                    i = j;
                    continue;
                }
            }
            const std::size_t end = skip_string(line, i, '\'');
            push_quoted(out, line, i, end);
            i = end;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]) && word_start)) {
            std::size_t end = i + 1;
            const bool hex = line[i] == '0' && i + 1 < line.size() && (line[i + 1] == 'x' || line[i + 1] == 'X');
            while (end < line.size() && (ident_char(line[end]) || (line[end] == '.' && end + 1 < line.size() && digit(line[end + 1])) ||
                                         ((line[end] == '+' || line[end] == '-') && (line[end - 1] == 'e' || line[end - 1] == 'E') && !hex))) {
                ++end;
            }
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (ident_start(c)) {
            std::size_t end = i;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string word(line.substr(i, end - i));
            const std::size_t next = skip_spaces(line, end);
            std::size_t prev = i;
            while (prev > 0 && (line[prev - 1] == ' ' || line[prev - 1] == '\t')) --prev;
            const bool member = prev > 0 && (line[prev - 1] == '.' || (syn.lua_long && line[prev - 1] == ':' && !(prev > 1 && line[prev - 2] == ':'))) && !(prev > 1 && line[prev - 2] == '.');
            if (syn.rust_macros && end < line.size() && line[end] == '!' && !(end + 1 < line.size() && line[end + 1] == '=')) {
                push(out, i, end + 1, CodeTokenKind::Preprocessor);
                i = end + 1;
                continue;
            }
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (after_def) {
                kind = word == "class" ? CodeTokenKind::Keyword : (syn.def_names && !syn.lua_long && std::isupper(static_cast<unsigned char>(word[0])) ? CodeTokenKind::Type : CodeTokenKind::Function);
                after_def = false;
            } else if (!member && syn.control.count(word)) {
                kind = CodeTokenKind::ControlKeyword;
            } else if (!member && syn.self_words.count(word)) {
                kind = CodeTokenKind::ThisKeyword;
            } else if (!member && syn.keywords.count(word)) {
                kind = CodeTokenKind::Keyword;
                if (syn.def_names && (word == "def" || word == "class" || word == "function")) after_def = true;
            } else if (!member && syn.constants.count(word)) {
                kind = CodeTokenKind::Constant;
            } else if (!member && syn.types.count(word)) {
                kind = CodeTokenKind::BuiltinType;
            } else if (!member && syn.builtins.count(word)) {
                kind = CodeTokenKind::BuiltinType;
            } else if (next < line.size() && line[next] == '(') {
                kind = CodeTokenKind::Function;
            } else if (member) {
                kind = CodeTokenKind::Property;
            } else if (all_upper(word)) {
                kind = CodeTokenKind::Constant;
            } else if (std::isupper(static_cast<unsigned char>(word[0])) && !syn.lua_long) {
                kind = CodeTokenKind::Type;
            } else if (syn.rust_macros && next + 1 < line.size() && line[next] == ':' && line[next + 1] == ':') {
                kind = CodeTokenKind::Namespace;
            }
            push(out, i, end, kind);
            i = end;
            continue;
        }
        if (c == ' ' || c == '\t') {
            std::size_t end = i;
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
            push(out, i, end, CodeTokenKind::Plain);
            i = end;
            continue;
        }
        push(out, i, i + 1, std::ispunct(static_cast<unsigned char>(c)) && c != ';' && c != ',' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

const std::set<std::string>& asm_jumps() {
    static const std::set<std::string> k = {
        "jmp", "je", "jne", "jz", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe", "jc", "jnc", "jo", "jno", "js", "jns", "jp", "jnp", "jecxz",
        "jrcxz", "call", "ret", "retq", "callq", "jmpq", "loop", "loope", "loopne", "syscall", "int", "iret", "iretq", "sysenter", "hlt", "b", "bl", "blr",
        "br", "bx", "blx", "beq", "bne", "bgt", "bge", "blt", "ble", "bhi", "bhs", "blo", "bls", "bmi", "bpl", "cbz", "cbnz", "tbz", "tbnz", "svc", "eret",
        "j", "jal", "jalr", "jr", "beqz", "bnez", "bltz", "bgez", "blez", "bgtz", "bltu", "bgeu", "ecall", "ebreak", "mret", "sret", "b.eq", "b.ne",
        "b.gt", "b.ge", "b.lt", "b.le", "b.hi", "b.hs", "b.lo", "b.ls", "b.mi", "b.pl", "b.cs", "b.cc", "b.vs", "b.vc",
    };
    return k;
}

const std::set<std::string>& asm_directives() {
    static const std::set<std::string> k = {
        "section", "segment", "global", "globl", "extern", "bits", "default", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq",
        "rest", "reso", "equ", "times", "incbin", "align", "alignb", "org", "struc", "endstruc", "istruc", "iend", "use16", "use32", "use64",
        "proc", "endp", "end", "include", "includelib", "model", "data", "code", "stack", "const", "public", "externdef", "proto", "invoke",
        "real4", "real8", "ends", "assume", "macro", "endm", "local", "format", "entry", "import", "label", "option", "comm", "common", "cpu", "absolute",
    };
    return k;
}

const std::set<std::string>& asm_sizes() {
    static const std::set<std::string> k = { "byte", "word", "dword", "qword", "tword", "oword", "yword", "zword", "ptr", "near", "far", "short", "xmmword",
                                             "ymmword", "zmmword", "rel", "abs", "wrt", "strict", "nosplit", "seg" };
    return k;
}

bool asm_data_word(const std::string& w) {
    static const std::set<std::string> k = { "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq", "rest", "reso", "equ", "times",
                                             "byte", "word", "dword", "qword", "real4", "real8", "label", "proc", "macro", "struc", "struct", "segment", "ends", "endp" };
    return k.count(w) > 0;
}

bool asm_register(const std::string& w) {
    static const std::set<std::string> fixed = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "rip", "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip", "ax", "bx", "cx",
        "dx", "si", "di", "bp", "sp", "ip", "al", "bl", "cl", "dl", "ah", "bh", "ch", "dh", "sil", "dil", "bpl", "spl", "cs", "ds", "es", "fs", "gs", "ss",
        "eflags", "rflags", "lr", "pc", "fp", "xzr", "wzr", "zero", "ra", "gp", "tp", "cpsr", "spsr", "apsr", "fpscr", "nzcv",
    };
    if (fixed.count(w)) return true;
    auto numbered = [&](std::string_view prefix, int most) {
        if (w.size() <= prefix.size() || w.compare(0, prefix.size(), prefix) != 0) return false;
        std::string digits = w.substr(prefix.size());
        if (prefix == "r") {
            while (!digits.empty() && (digits.back() == 'b' || digits.back() == 'w' || digits.back() == 'd')) digits.pop_back();
        }
        if (digits.empty() || digits.size() > 2 || digits.find_first_not_of("0123456789") != std::string::npos) return false;
        return std::stoi(digits) <= most;
    };
    return numbered("r", 31) || numbered("x", 30) || numbered("w", 30) || numbered("xmm", 31) || numbered("ymm", 31) || numbered("zmm", 31) ||
           numbered("mm", 7) || numbered("k", 7) || numbered("a", 7) || numbered("t", 9) || numbered("s", 11) || numbered("v", 31) || numbered("q", 31) ||
           numbered("d", 31) || numbered("f", 31) || numbered("fa", 7) || numbered("ft", 11) || numbered("fs", 11) || numbered("st", 7) || numbered("cr", 8) ||
           numbered("dr", 7);
}

std::vector<CodeToken> highlight_assembly(std::string_view line, CodeLineState& state) {
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
    const std::size_t first = skip_spaces(line, i);
    bool statement_start = true;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ';' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            push(out, i, line.size(), CodeTokenKind::Comment);
            break;
        }
        if (c == '#' && i == first && i + 1 < line.size() && std::isalpha(static_cast<unsigned char>(line[i + 1]))) {
            push(out, i, line.size(), CodeTokenKind::Preprocessor);
            break;
        }
        if ((c == '#' || c == '@') && (i + 1 >= line.size() || line[i + 1] == ' ' || line[i + 1] == '\t' || line[i + 1] == '#' || line[i + 1] == '!')) {
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
        if (c == '"' || c == '\'' || c == '`') {
            const std::size_t end = skip_string(line, i, c);
            push_quoted(out, line, i, end);
            i = end;
            statement_start = false;
            continue;
        }
        if (c == ' ' || c == '\t') {
            std::size_t end = i;
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
            push(out, i, end, CodeTokenKind::Plain);
            i = end;
            continue;
        }
        if ((c == '$' || c == '#') && i + 1 < line.size() && (digit(line[i + 1]) || line[i + 1] == '-')) {
            std::size_t end = i + 2;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '.')) ++end;
            push(out, i, end, CodeTokenKind::Number);
            i = end;
            continue;
        }
        if (digit(c)) {
            std::size_t end = i;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '.')) ++end;
            const std::string_view text = line.substr(i, end - i);
            const bool label = statement_start && end < line.size() && line[end] == ':';
            const bool label_ref = text.size() >= 2 && (text.back() == 'f' || text.back() == 'b') && text.find_first_not_of("0123456789") == text.size() - 1;
            if (label) {
                push(out, i, end + 1, CodeTokenKind::Function);
                i = end + 1;
                continue;
            }
            push(out, i, end, label_ref ? CodeTokenKind::Function : CodeTokenKind::Number);
            i = end;
            statement_start = false;
            continue;
        }
        if (c == '%' || c == '$' || ident_start(c) || c == '.' || c == '?') {
            std::size_t end = i + 1;
            while (end < line.size() && (ident_char(line[end]) || line[end] == '.' || line[end] == '$' || line[end] == '?' || line[end] == '@')) ++end;
            const std::string word = lower(line.substr(i, end - i));
            const std::string bare = (word[0] == '%' || word[0] == '$') ? word.substr(1) : word;
            const std::size_t next = skip_spaces(line, end);
            if (statement_start && next < line.size() && line[next] == ':' && !(next + 1 < line.size() && (line[next + 1] == ':' || line[next + 1] == '='))) {
                push(out, i, next + 1, CodeTokenKind::Function);
                i = next + 1;
                continue;
            }
            CodeTokenKind kind = CodeTokenKind::Plain;
            if (word[0] != '.' && asm_register(bare)) kind = CodeTokenKind::Variable;
            else if (word[0] == '%') kind = CodeTokenKind::Preprocessor;
            else if (word[0] == '.' && statement_start) kind = CodeTokenKind::Preprocessor;
            else if (statement_start && asm_directives().count(bare)) kind = CodeTokenKind::Preprocessor;
            else if (statement_start && next > end && asm_data_word(lower(line.substr(next, std::min(line.size(), line.find_first_of(" \t,", next)) - next)))) kind = CodeTokenKind::Function;
            else if (statement_start) kind = asm_jumps().count(bare) ? CodeTokenKind::ControlKeyword : CodeTokenKind::Keyword;
            else if (asm_sizes().count(bare)) kind = CodeTokenKind::BuiltinType;
            else if (asm_directives().count(bare)) kind = CodeTokenKind::Preprocessor;
            push(out, i, end, kind);
            i = end;
            if (!(kind == CodeTokenKind::Function && statement_start)) statement_start = false;
            continue;
        }
        push(out, i, i + 1, std::ispunct(static_cast<unsigned char>(c)) && c != ',' ? CodeTokenKind::Operator : CodeTokenKind::Plain);
        ++i;
    }
    return out;
}

void add_syntax_words(std::vector<CodeWord>& out, const Syntax& syn) {
    for (const std::string& w : syn.control) out.push_back(CodeWord{ w, CodeTokenKind::ControlKeyword });
    for (const std::string& w : syn.keywords) out.push_back(CodeWord{ w, CodeTokenKind::Keyword });
    for (const std::string& w : syn.types) out.push_back(CodeWord{ w, CodeTokenKind::BuiltinType });
    for (const std::string& w : syn.constants) out.push_back(CodeWord{ w, CodeTokenKind::Constant });
    for (const std::string& w : syn.self_words) out.push_back(CodeWord{ w, CodeTokenKind::ThisKeyword });
    for (const std::string& w : syn.builtins) out.push_back(CodeWord{ w, CodeTokenKind::Function });
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
        case CodeLanguage::CMake: return "cmake";
        case CodeLanguage::Html: return "html";
        case CodeLanguage::Css: return "css";
        case CodeLanguage::JavaScript: return "javascript";
        case CodeLanguage::Json: return "json";
        case CodeLanguage::Rust: return "rust";
        case CodeLanguage::Zig: return "zig";
        case CodeLanguage::Python: return "python";
        case CodeLanguage::Lua: return "lua";
        case CodeLanguage::Java: return "java";
        case CodeLanguage::CSharp: return "csharp";
        case CodeLanguage::Assembly: return "asm";
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
        case CodeTokenKind::Namespace: return "namespace";
        case CodeTokenKind::Variable: return "variable";
        case CodeTokenKind::Escape: return "escape";
        case CodeTokenKind::ControlKeyword: return "control-keyword";
        case CodeTokenKind::BuiltinType: return "builtin-type";
        case CodeTokenKind::Declaration: return "declaration";
        case CodeTokenKind::SelfKeyword: return "self";
        case CodeTokenKind::ThisKeyword: return "this";
        case CodeTokenKind::Cast: return "cast";
        case CodeTokenKind::BuiltinOperator: return "builtin-operator";
        case CodeTokenKind::OverloadedOperator: return "overloaded-operator";
        case CodeTokenKind::EnumMember: return "enum-member";
        case CodeTokenKind::TemplateScope: return "template-scope";
        case CodeTokenKind::VariableTemplate: return "variable-template";
        case CodeTokenKind::Unresolved: return "unresolved";
    }
    return "plain";
}

CodeLanguage code_language_from_name(std::string_view name) noexcept {
    const std::string n = lower(name);
    if (n == "c" || n == "h") return CodeLanguage::C;
    if (n == "cpp" || n == "c++" || n == "cxx" || n == "cc" || n == "hpp" || n == "hh" || n == "hxx") return CodeLanguage::Cpp;
    if (n == "walnut" || n == "wal" || n == "walh" || n == "hwal") return CodeLanguage::Walnut;
    if (n == "html" || n == "htm" || n == "xhtml") return CodeLanguage::Html;
    if (n == "css") return CodeLanguage::Css;
    if (n == "javascript" || n == "js" || n == "mjs" || n == "cjs" || n == "jsx") return CodeLanguage::JavaScript;
    if (n == "json" || n == "jsonc" || n == "webmanifest") return CodeLanguage::Json;
    if (n == "grove" || n == "xml" || n == "markup" || n == "svg") return CodeLanguage::Grove;
    if (n == "bark") return CodeLanguage::Bark;
    if (n == "cmake" || n == "cmakelists" || n == "cmakelists.txt") return CodeLanguage::CMake;
    if (n == "rust" || n == "rs") return CodeLanguage::Rust;
    if (n == "zig" || n == "zon") return CodeLanguage::Zig;
    if (n == "python" || n == "py" || n == "pyw" || n == "pyi") return CodeLanguage::Python;
    if (n == "lua") return CodeLanguage::Lua;
    if (n == "java") return CodeLanguage::Java;
    if (n == "csharp" || n == "c#" || n == "cs" || n == "csx") return CodeLanguage::CSharp;
    if (n == "asm" || n == "assembly" || n == "s" || n == "nasm" || n == "masm" || n == "gas" || n == "inc") return CodeLanguage::Assembly;
    return CodeLanguage::Plain;
}

CodeLanguage code_language_for_path(std::string_view path) noexcept {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (lower(name) == "cmakelists.txt") return CodeLanguage::CMake;
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return CodeLanguage::Plain;
    return code_language_from_name(name.substr(dot + 1));
}

const std::vector<CodeLanguage>& code_languages() noexcept {
    static const std::vector<CodeLanguage> all = { CodeLanguage::C, CodeLanguage::Cpp, CodeLanguage::Walnut, CodeLanguage::Grove, CodeLanguage::Bark, CodeLanguage::CMake,
                                                   CodeLanguage::Html, CodeLanguage::Css, CodeLanguage::JavaScript, CodeLanguage::Json, CodeLanguage::Rust,
                                                   CodeLanguage::Zig, CodeLanguage::Python, CodeLanguage::Lua, CodeLanguage::Java, CodeLanguage::CSharp,
                                                   CodeLanguage::Assembly };
    return all;
}

std::string code_line_comment(CodeLanguage language, std::string_view sample) {
    switch (language) {
        case CodeLanguage::C:
        case CodeLanguage::Cpp:
        case CodeLanguage::Walnut:
        case CodeLanguage::JavaScript:
        case CodeLanguage::Rust:
        case CodeLanguage::Zig:
        case CodeLanguage::Java:
        case CodeLanguage::CSharp:
        case CodeLanguage::Bark: return "//";
        case CodeLanguage::Python:
        case CodeLanguage::CMake: return "#";
        case CodeLanguage::Lua: return "--";
        case CodeLanguage::Assembly: {
            std::size_t semi = 0;
            std::size_t hash = 0;
            std::size_t slashes = 0;
            std::size_t at = 0;
            bool gas = false;
            bool arm64 = false;
            std::size_t start = 0;
            while (start < sample.size()) {
                std::size_t end = sample.find('\n', start);
                if (end == std::string_view::npos) end = sample.size();
                const std::string_view row = sample.substr(start, end - start);
                start = end + 1;
                const std::size_t first = row.find_first_not_of(" \t");
                if (first == std::string_view::npos) continue;
                const char c = row[first];
                if (c == ';') ++semi;
                else if (c == '#' && (first + 1 >= row.size() || row[first + 1] == ' ')) ++hash;
                else if (row.compare(first, 2, "//") == 0) ++slashes;
                else if (c == '@' && (first + 1 >= row.size() || row[first + 1] == ' ')) ++at;
                if (row.find(".globl") != std::string_view::npos || row.find(".global") != std::string_view::npos || row.find(".section") != std::string_view::npos ||
                    row.find("%r") != std::string_view::npos || row.find("%e") != std::string_view::npos) {
                    gas = true;
                }
                if (row.find(" x0") != std::string_view::npos || row.find(" w0") != std::string_view::npos || row.find("stp ") != std::string_view::npos ||
                    row.find("ldp ") != std::string_view::npos) {
                    arm64 = true;
                }
            }
            const std::size_t most = std::max({ semi, hash, slashes, at });
            if (most > 0) {
                if (most == semi) return ";";
                if (most == hash) return "#";
                if (most == slashes) return "//";
                return "@";
            }
            if (arm64) return "//";
            return gas ? "#" : ";";
        }
        default: return "";
    }
}

const std::vector<CodeTokenKind>& code_token_kinds() noexcept {
    static const std::vector<CodeTokenKind> all = {
        CodeTokenKind::Keyword, CodeTokenKind::Type, CodeTokenKind::Function, CodeTokenKind::Namespace, CodeTokenKind::Variable,
        CodeTokenKind::Constant, CodeTokenKind::String, CodeTokenKind::Escape, CodeTokenKind::Number, CodeTokenKind::Comment,
        CodeTokenKind::Preprocessor, CodeTokenKind::Operator, CodeTokenKind::Tag, CodeTokenKind::Attribute, CodeTokenKind::Property,
        CodeTokenKind::ControlKeyword, CodeTokenKind::BuiltinType, CodeTokenKind::Declaration, CodeTokenKind::SelfKeyword,
        CodeTokenKind::ThisKeyword, CodeTokenKind::Cast, CodeTokenKind::BuiltinOperator, CodeTokenKind::OverloadedOperator,
        CodeTokenKind::EnumMember, CodeTokenKind::TemplateScope, CodeTokenKind::VariableTemplate, CodeTokenKind::Unresolved,
    };
    return all;
}

std::optional<CodeTokenKind> code_token_kind_from_name(std::string_view name) noexcept {
    std::string n = lower(name);
    std::replace(n.begin(), n.end(), '_', '-');
    std::replace(n.begin(), n.end(), ' ', '-');
    if (n == "plain") return CodeTokenKind::Plain;
    if (n == "built-in-type") return CodeTokenKind::BuiltinType;
    if (n == "built-in-operator") return CodeTokenKind::BuiltinOperator;
    for (CodeTokenKind kind : code_token_kinds()) {
        if (n == to_string(kind)) return kind;
    }
    return std::nullopt;
}

CodeTokenKind code_token_fallback(CodeTokenKind kind) noexcept {
    switch (kind) {
        case CodeTokenKind::ControlKeyword:
        case CodeTokenKind::SelfKeyword:
        case CodeTokenKind::ThisKeyword:
        case CodeTokenKind::Cast: return CodeTokenKind::Keyword;
        case CodeTokenKind::BuiltinType:
        case CodeTokenKind::TemplateScope:
        case CodeTokenKind::VariableTemplate: return CodeTokenKind::Type;
        case CodeTokenKind::Declaration: return CodeTokenKind::Variable;
        case CodeTokenKind::BuiltinOperator: return CodeTokenKind::Operator;
        case CodeTokenKind::OverloadedOperator: return CodeTokenKind::Function;
        case CodeTokenKind::EnumMember: return CodeTokenKind::Constant;
        case CodeTokenKind::Unresolved: return CodeTokenKind::Plain;
        default: return kind;
    }
}

const std::vector<CodeTokenKind>& code_token_kinds_for(CodeLanguage language) noexcept {
    using K = CodeTokenKind;
    static const std::vector<K> c = { K::Keyword, K::ControlKeyword, K::Type, K::BuiltinType, K::Function, K::Constant, K::String, K::Escape,
                                      K::Number, K::Comment, K::Preprocessor, K::Operator, K::Attribute };
    static const std::vector<K> cpp = { K::Keyword, K::ControlKeyword, K::Type, K::BuiltinType, K::Function, K::Namespace, K::ThisKeyword, K::Cast,
                                        K::Constant, K::String, K::Escape, K::Number, K::Comment, K::Preprocessor, K::Operator, K::Attribute };
    static const std::vector<K> walnut = { K::Declaration, K::Variable, K::VariableTemplate, K::Unresolved, K::SelfKeyword, K::Cast, K::ThisKeyword,
                                           K::BuiltinOperator, K::OverloadedOperator, K::Function, K::TemplateScope, K::Type, K::BuiltinType,
                                           K::Namespace, K::EnumMember, K::Keyword, K::ControlKeyword, K::Constant, K::String, K::Escape, K::Number,
                                           K::Comment, K::Preprocessor };
    static const std::vector<K> grove = { K::Tag, K::Attribute, K::String, K::Constant, K::Comment, K::Operator };
    static const std::vector<K> bark = { K::Tag, K::Attribute, K::Property, K::Keyword, K::Function, K::Constant, K::Number, K::String, K::Comment, K::Operator };
    static const std::vector<K> cmake = { K::Function, K::Keyword, K::Variable, K::Attribute, K::Constant, K::String, K::Escape, K::Number, K::Comment };
    static const std::vector<K> html = { K::Tag, K::Attribute, K::String, K::Constant, K::Comment, K::Operator, K::Keyword, K::ControlKeyword, K::Function,
                                         K::Property, K::BuiltinType, K::Number };
    static const std::vector<K> javascript = { K::Keyword, K::ControlKeyword, K::ThisKeyword, K::Function, K::Property, K::Type, K::BuiltinType, K::Constant,
                                               K::String, K::Escape, K::Number, K::Comment, K::Operator };
    static const std::vector<K> css = { K::Tag, K::Attribute, K::Keyword, K::ControlKeyword, K::Property, K::Variable, K::Function, K::Constant, K::Number,
                                        K::String, K::Comment, K::Operator };
    static const std::vector<K> json = { K::Property, K::String, K::Escape, K::Number, K::Constant, K::Operator };
    static const std::vector<K> general = { K::Keyword, K::ControlKeyword, K::ThisKeyword, K::Type, K::BuiltinType, K::Function, K::Property, K::Namespace,
                                            K::Constant, K::String, K::Escape, K::Number, K::Comment, K::Operator, K::Attribute, K::Preprocessor };
    static const std::vector<K> assembly = { K::Keyword, K::ControlKeyword, K::Variable, K::Function, K::Preprocessor, K::BuiltinType, K::Number,
                                             K::String, K::Escape, K::Comment, K::Operator };
    switch (language) {
        case CodeLanguage::C: return c;
        case CodeLanguage::Cpp: return cpp;
        case CodeLanguage::Walnut: return walnut;
        case CodeLanguage::Grove: return grove;
        case CodeLanguage::Bark: return bark;
        case CodeLanguage::CMake: return cmake;
        case CodeLanguage::Html: return html;
        case CodeLanguage::Css: return css;
        case CodeLanguage::JavaScript: return javascript;
        case CodeLanguage::Json: return json;
        case CodeLanguage::Rust:
        case CodeLanguage::Zig:
        case CodeLanguage::Python:
        case CodeLanguage::Lua:
        case CodeLanguage::Java:
        case CodeLanguage::CSharp: return general;
        case CodeLanguage::Assembly: return assembly;
        case CodeLanguage::Plain: break;
    }
    return code_token_kinds();
}

std::vector<CodeToken> highlight_code_line(CodeLanguage language, std::string_view line, CodeLineState& state) {
    switch (language) {
        case CodeLanguage::C:
        case CodeLanguage::Cpp: return highlight_c_like(language, line, state);
        case CodeLanguage::Walnut: return highlight_walnut(line, state);
        case CodeLanguage::Grove: return highlight_markup(line, state);
        case CodeLanguage::Bark: return highlight_bark(line, state);
        case CodeLanguage::CMake: return highlight_cmake(line, state);
        case CodeLanguage::Html: return highlight_html(line, state);
        case CodeLanguage::Css: return highlight_css(line, state);
        case CodeLanguage::JavaScript: return highlight_javascript(line, state);
        case CodeLanguage::Json: return highlight_json(line, state);
        case CodeLanguage::Rust: return highlight_generic(rust_syntax(), line, state);
        case CodeLanguage::Zig: return highlight_generic(zig_syntax(), line, state);
        case CodeLanguage::Python: return highlight_generic(python_syntax(), line, state);
        case CodeLanguage::Lua: return highlight_generic(lua_syntax(), line, state);
        case CodeLanguage::Java: return highlight_generic(java_syntax(), line, state);
        case CodeLanguage::CSharp: return highlight_generic(csharp_syntax(), line, state);
        case CodeLanguage::Assembly: return highlight_assembly(line, state);
        case CodeLanguage::Plain: break;
    }
    std::vector<CodeToken> out;
    if (!line.empty()) out.push_back(CodeToken{ 0, line.size(), CodeTokenKind::Plain });
    return out;
}

std::uint64_t code_line_hash(std::string_view line) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (char c : line) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

namespace {

std::string hex_hash(std::uint64_t h) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[h & 15];
        h >>= 4;
    }
    return out;
}

template <typename T, typename Fn>
std::string overlay_text(const std::vector<std::string>& lines, const std::vector<T>& items, Fn&& code) {
    std::map<std::size_t, std::vector<const T*>> by_line;
    for (const T& item : items) {
        if (item.line < lines.size()) by_line[item.line].push_back(&item);
    }
    std::string out;
    for (const auto& [line, list] : by_line) {
        out += std::to_string(line) + ' ' + hex_hash(code_line_hash(lines[line])) + ' ';
        for (std::size_t k = 0; k < list.size(); ++k) {
            if (k) out += ',';
            out += std::to_string(list[k]->column) + ':' + std::to_string(list[k]->length) + ':' + code(*list[k]);
        }
        out += '\n';
    }
    return out;
}

template <typename T, typename Fn>
void parse_overlay(std::string_view text, std::map<std::uint64_t, std::vector<std::pair<std::size_t, std::vector<T>>>>& table, Fn&& make) {
    table.clear();
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string row(text.substr(start, end - start));
        start = end + 1;
        const std::size_t a = row.find(' ');
        const std::size_t b = a == std::string::npos ? std::string::npos : row.find(' ', a + 1);
        if (b == std::string::npos) continue;
        const std::size_t line = static_cast<std::size_t>(std::strtoull(row.c_str(), nullptr, 10));
        const std::uint64_t hash = std::strtoull(row.substr(a + 1, b - a - 1).c_str(), nullptr, 16);
        std::vector<T> items;
        std::size_t at = b + 1;
        while (at < row.size()) {
            std::size_t stop = row.find(',', at);
            if (stop == std::string::npos) stop = row.size();
            const std::string item = row.substr(at, stop - at);
            at = stop + 1;
            const std::size_t c1 = item.find(':');
            const std::size_t c2 = c1 == std::string::npos ? std::string::npos : item.find(':', c1 + 1);
            if (c2 == std::string::npos) continue;
            const std::size_t column = static_cast<std::size_t>(std::strtoull(item.c_str(), nullptr, 10));
            const std::size_t length = static_cast<std::size_t>(std::strtoull(item.c_str() + c1 + 1, nullptr, 10));
            if (auto made = make(column, column + length, item.substr(c2 + 1))) items.push_back(*made);
        }
        std::sort(items.begin(), items.end(), [](const T& x, const T& y) { return x.begin < y.begin; });
        table[hash].push_back({ line, std::move(items) });
    }
}

} // namespace

std::string code_spans_text(const std::vector<std::string>& lines, const std::vector<CodeSpan>& spans) {
    return overlay_text(lines, spans, [](const CodeSpan& s) { return std::string(to_string(s.kind)); });
}

std::string code_problems_text(const std::vector<std::string>& lines, const std::vector<CodeProblem>& problems) {
    return overlay_text(lines, problems, [](const CodeProblem& p) { return std::string(p.kind == CodeProblemKind::Warning ? "warning" : "error"); });
}

std::vector<CodeToken> overlay_code_tokens(const std::vector<CodeToken>& base, const std::vector<CodeToken>& over, std::size_t length) {
    if (over.empty()) return base;
    std::vector<CodeTokenKind> kinds(length, CodeTokenKind::Plain);
    for (const CodeToken& t : base) {
        for (std::size_t k = t.begin; k < std::min(t.end, length); ++k) kinds[k] = t.kind;
    }
    for (const CodeToken& t : over) {
        for (std::size_t k = t.begin; k < std::min(t.end, length); ++k) kinds[k] = t.kind;
    }
    std::vector<CodeToken> out;
    for (std::size_t k = 0; k < length; ++k) push(out, k, k + 1, kinds[k]);
    return out;
}

template <typename T>
const std::vector<T>* CodeOverlay::nearest(const Table<T>& table, std::size_t line, std::string_view text) {
    const auto it = table.find(code_line_hash(text));
    if (it == table.end()) return nullptr;
    const std::vector<T>* best = nullptr;
    std::size_t distance = static_cast<std::size_t>(-1);
    for (const auto& [at, items] : it->second) {
        const std::size_t d = at > line ? at - line : line - at;
        if (d < distance) {
            distance = d;
            best = &items;
        }
    }
    return best;
}

void CodeOverlay::set_spans(std::string_view text) {
    parse_overlay(text, m_spans, [](std::size_t begin, std::size_t end, const std::string& name) -> std::optional<CodeToken> {
        const auto kind = code_token_kind_from_name(name);
        if (!kind || end <= begin) return std::nullopt;
        return CodeToken{ begin, end, *kind };
    });
}

void CodeOverlay::set_problems(std::string_view text) {
    parse_overlay(text, m_problems, [](std::size_t begin, std::size_t end, const std::string& name) -> std::optional<CodeProblemMark> {
        return CodeProblemMark{ begin, std::max(end, begin), name == "warning" ? CodeProblemKind::Warning : CodeProblemKind::Error };
    });
}

void CodeOverlay::clear() {
    m_spans.clear();
    m_problems.clear();
}

std::vector<CodeToken> CodeOverlay::spans(std::size_t line, std::string_view text) const {
    const std::vector<CodeToken>* found = nearest(m_spans, line, text);
    return found ? *found : std::vector<CodeToken>{};
}

std::vector<CodeProblemMark> CodeOverlay::problems(std::size_t line, std::string_view text) const {
    const std::vector<CodeProblemMark>* found = nearest(m_problems, line, text);
    return found ? *found : std::vector<CodeProblemMark>{};
}

bool CodeOverlay::has_error(std::size_t line, std::string_view text) const {
    for (const CodeProblemMark& p : problems(line, text)) {
        if (p.kind == CodeProblemKind::Error) return true;
    }
    return false;
}

std::vector<CodeWord> code_language_words(CodeLanguage language) {
    std::vector<CodeWord> out;
    auto add = [&](const std::set<std::string>& words, CodeTokenKind kind) {
        for (const std::string& w : words) out.push_back(CodeWord{ w, kind });
    };
    switch (language) {
        case CodeLanguage::C:
            add(c_keywords(), CodeTokenKind::Keyword);
            add(c_types(), CodeTokenKind::BuiltinType);
            add(constants(), CodeTokenKind::Constant);
            break;
        case CodeLanguage::Cpp:
            add(cpp_keywords(), CodeTokenKind::Keyword);
            add(c_types(), CodeTokenKind::BuiltinType);
            add(constants(), CodeTokenKind::Constant);
            break;
        case CodeLanguage::Walnut:
            add(walnut_keywords(), CodeTokenKind::Keyword);
            add(walnut_types(), CodeTokenKind::BuiltinType);
            add(constants(), CodeTokenKind::Constant);
            break;
        case CodeLanguage::JavaScript:
        case CodeLanguage::Html:
            add(js_control(), CodeTokenKind::ControlKeyword);
            add(js_keywords(), CodeTokenKind::Keyword);
            add(js_constants(), CodeTokenKind::Constant);
            add(js_globals(), CodeTokenKind::BuiltinType);
            break;
        case CodeLanguage::CMake:
            add(cmake_control(), CodeTokenKind::Keyword);
            add(cmake_constants(), CodeTokenKind::Constant);
            add(cmake_operators(), CodeTokenKind::Operator);
            break;
        case CodeLanguage::Rust: add_syntax_words(out, rust_syntax()); break;
        case CodeLanguage::Zig: add_syntax_words(out, zig_syntax()); break;
        case CodeLanguage::Python: add_syntax_words(out, python_syntax()); break;
        case CodeLanguage::Lua: add_syntax_words(out, lua_syntax()); break;
        case CodeLanguage::Java: add_syntax_words(out, java_syntax()); break;
        case CodeLanguage::CSharp: add_syntax_words(out, csharp_syntax()); break;
        case CodeLanguage::Assembly:
            add(asm_jumps(), CodeTokenKind::ControlKeyword);
            add(asm_directives(), CodeTokenKind::Preprocessor);
            break;
        default:
            break;
    }
    return out;
}

} // namespace grove
