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
            if (kind != CodeTokenKind::Keyword) next_name = CodeTokenKind::Plain;
            if (word == "class" || word == "struct" || word == "enum" || word == "union" || word == "concept" || word == "typename") next_name = CodeTokenKind::Type;
            else if (word == "namespace" || word == "module") next_name = CodeTokenKind::Namespace;
            else if (kind == CodeTokenKind::Keyword && word != "enum") next_name = CodeTokenKind::Plain;
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
    if (n == "cmake" || n == "cmakelists" || n == "cmakelists.txt") return CodeLanguage::CMake;
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
    static const std::vector<CodeLanguage> all = { CodeLanguage::C, CodeLanguage::Cpp, CodeLanguage::Walnut, CodeLanguage::Grove, CodeLanguage::Bark, CodeLanguage::CMake };
    return all;
}

const std::vector<CodeTokenKind>& code_token_kinds() noexcept {
    static const std::vector<CodeTokenKind> all = {
        CodeTokenKind::Keyword, CodeTokenKind::Type, CodeTokenKind::Function, CodeTokenKind::Namespace, CodeTokenKind::Variable,
        CodeTokenKind::Constant, CodeTokenKind::String, CodeTokenKind::Escape, CodeTokenKind::Number, CodeTokenKind::Comment,
        CodeTokenKind::Preprocessor, CodeTokenKind::Operator, CodeTokenKind::Tag, CodeTokenKind::Attribute, CodeTokenKind::Property,
    };
    return all;
}

std::optional<CodeTokenKind> code_token_kind_from_name(std::string_view name) noexcept {
    const std::string n = lower(name);
    if (n == "plain") return CodeTokenKind::Plain;
    for (CodeTokenKind kind : code_token_kinds()) {
        if (n == to_string(kind)) return kind;
    }
    return std::nullopt;
}

std::vector<CodeToken> highlight_code_line(CodeLanguage language, std::string_view line, CodeLineState& state) {
    switch (language) {
        case CodeLanguage::C:
        case CodeLanguage::Cpp:
        case CodeLanguage::Walnut: return highlight_c_like(language, line, state);
        case CodeLanguage::Grove: return highlight_markup(line, state);
        case CodeLanguage::Bark: return highlight_bark(line, state);
        case CodeLanguage::CMake: return highlight_cmake(line, state);
        case CodeLanguage::Plain: break;
    }
    std::vector<CodeToken> out;
    if (!line.empty()) out.push_back(CodeToken{ 0, line.size(), CodeTokenKind::Plain });
    return out;
}

} // namespace grove
