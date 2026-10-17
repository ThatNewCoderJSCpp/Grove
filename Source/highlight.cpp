#include "grove/highlight.hpp"
#include "grove/project.hpp"
#include "grove/bark/parser.hpp"
#include "grove/input/input.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace grove {

namespace {

struct Token {
    std::size_t line = 0;
    std::size_t col = 0;
    std::size_t len = 0;
    const char* type = "";
    const char* modifier = nullptr;
};

void json_string(std::ostream& os, std::string_view s) {
    os << '"';
    for (const char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    os << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    os << c;
                }
        }
    }
    os << '"';
}

std::size_t line0(const Position& p) { return p.line > 0 ? p.line - 1 : 0; }
std::size_t col0(const Position& p) { return p.column > 0 ? p.column - 1 : 0; }

bool token_for(const Mark& m, Token& t) {
    if (m.span.begin.line != m.span.end.line || m.span.end.column <= m.span.begin.column) return false;
    t.line = line0(m.span.begin);
    t.col = col0(m.span.begin);
    t.len = m.span.end.column - m.span.begin.column;
    switch (m.kind) {
        case MarkKind::Tag:
        case MarkKind::SelectorTag: t.type = "tag"; break;
        case MarkKind::InputTag: t.type = "tag"; t.modifier = "input"; break;
        case MarkKind::CustomTag: t.type = "tag"; t.modifier = "custom"; break;
        case MarkKind::Property: t.type = "property"; break;
        case MarkKind::Variable: t.type = "variable"; break;
        case MarkKind::VariableDeclaration: t.type = "variable"; t.modifier = "declaration"; break;
        case MarkKind::UnknownTag:
        case MarkKind::UnknownProperty:
        case MarkKind::UnresolvedVariable:
        case MarkKind::UnknownSelectorTag: t.type = "unresolved"; break;
        case MarkKind::Color: return false;
    }
    return true;
}

void write_json(std::ostream& os, const Marks& marks, const Diagnostics& diagnostics, const std::string& path) {
    std::vector<Token> tokens;
    for (const Mark& m : marks) {
        Token t;
        if (token_for(m, t)) tokens.push_back(t);
    }
    std::sort(tokens.begin(), tokens.end(), [](const Token& a, const Token& b) { return a.line != b.line ? a.line < b.line : a.col < b.col; });

    os << "{\"tokens\":[";
    bool first = true;
    std::size_t last_line = 0;
    std::size_t last_end = 0;
    bool any = false;
    for (const Token& t : tokens) {
        if (any && t.line == last_line && t.col < last_end) continue;
        if (!first) os << ',';
        first = false;
        os << '[' << t.line << ',' << t.col << ',' << t.len << ",\"" << t.type << "\",[";
        if (t.modifier) os << '"' << t.modifier << '"';
        os << "]]";
        last_line = t.line;
        last_end = t.col + t.len;
        any = true;
    }

    os << "],\"colors\":[";
    first = true;
    for (const Mark& m : marks) {
        if (m.kind != MarkKind::Color) continue;
        if (!first) os << ',';
        first = false;
        os << "{\"line\":" << line0(m.span.begin) << ",\"col\":" << col0(m.span.begin)
           << ",\"endLine\":" << line0(m.span.end) << ",\"endCol\":" << col0(m.span.end)
           << ",\"r\":" << int(m.r) << ",\"g\":" << int(m.g) << ",\"b\":" << int(m.b) << ",\"a\":" << int(m.a) << '}';
    }

    os << "],\"diagnostics\":[";
    first = true;
    for (const Diagnostic& d : diagnostics.items()) {
        if (!d.file.empty() && d.file != path) continue;
        if (!first) os << ',';
        first = false;
        os << "{\"line\":" << line0(d.span.begin) << ",\"col\":" << col0(d.span.begin)
           << ",\"endLine\":" << line0(d.span.end) << ",\"endCol\":" << col0(d.span.end)
           << ",\"severity\":\"" << to_string(d.severity) << "\",\"message\":";
        json_string(os, d.message);
        os << '}';
    }
    os << "]}\n";
}

} // namespace

bool is_bark_path(std::string_view path) noexcept {
    return path.size() >= 5 && path.substr(path.size() - 5) == ".bark";
}

std::string highlight_json(std::string_view source, const std::string& path) {
    std::ostringstream os;
    if (is_bark_path(path)) {
        Diagnostics diagnostics;
        const bark::Stylesheet sheet = bark::parse_stylesheet(source, path, diagnostics);
        write_json(os, sheet.marks(), diagnostics, path);
        return os.str();
    }

    const std::string base_dir = std::filesystem::path(path).parent_path().string();
    Project project = load_project_from_source(source, path, base_dir);
    Marks marks = project.document.marks();
    project.document.root().walk([&marks](const Node& node) {
        if (input_kind_of(node) != InputKind::Color) return;
        const Attribute* a = node.find_attribute("value");
        if (!a || !a->has_value) return;
        if (const auto c = bark::parse_hex_color(a->value)) {
            Mark m{ MarkKind::Color, a->value_span };
            m.r = c->r;
            m.g = c->g;
            m.b = c->b;
            m.a = c->a;
            marks.push_back(m);
        }
    });
    write_json(os, marks, project.diagnostics, path);
    return os.str();
}

} // namespace grove
