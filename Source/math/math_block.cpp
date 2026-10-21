#include "grove/math/math_block.hpp"
#include "grove/markup/parser.hpp"

#include <mathtext.hpp>

namespace grove {

namespace {

Position advance_position(Position pos, std::string_view text, std::size_t count) {
    for (std::size_t i = 0; i < count && i < text.size(); ++i) {
        const char c = text[i];
        ++pos.offset;
        if (c == '\n') {
            ++pos.line;
            pos.column = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++pos.column;
        }
    }
    return pos;
}

const Node* raw_child(const Node& element) {
    for (const auto& c : element.children()) {
        if (c->is_raw()) return c.get();
    }
    return nullptr;
}

} // namespace

MathSyntax math_syntax_of(const Node& element, const std::string& file, Diagnostics& diagnostics) {
    const std::string s = element.attribute_or("syntax", "infix");
    if (s == "infix" || s == "math") return MathSyntax::Infix;
    if (s == "brace" || s == "mathtext") return MathSyntax::Brace;
    diagnostics.warning(file, element.span(), "unknown math syntax '" + s + "' (use \"infix\" or \"brace\"); using infix");
    return MathSyntax::Infix;
}

std::string math_source_of(const Node& element) {
    const Node* raw = raw_child(element);
    if (raw) return raw->text();
    std::string out;
    for (const auto& c : element.children()) {
        if (c->is_text()) out += c->text();
    }
    return out;
}

std::string to_brace_form(const MathBlock& block) {
    if (block.syntax == MathSyntax::Brace) return dedent(block.source);
    return fizmo::mathtext::math_to_brace(block.source);
}

bool check_math(const MathBlock& block, const std::string& file, Diagnostics& diagnostics) {
    const fizmo::mathtext::Document doc = block.syntax == MathSyntax::Brace ? fizmo::mathtext::parse(block.source) : fizmo::mathtext::parse_math(block.source);
    const Node* raw = block.element ? raw_child(*block.element) : nullptr;
    const Position origin = raw ? raw->text_span().begin : Position{};
    for (const fizmo::mathtext::Diagnostic& d : doc.diagnostics()) {
        const Position b = advance_position(origin, block.source, d.span.begin);
        const Position e = advance_position(origin, block.source, d.span.end());
        Severity sev = Severity::Error;
        if (d.severity == fizmo::mathtext::Severity::Warning) sev = Severity::Warning;
        else if (d.severity == fizmo::mathtext::Severity::Note) sev = Severity::Note;
        const std::string message = d.message.empty() ? fizmo::mathtext::diagnostic_text(d.code) : d.message;
        diagnostics.add(sev, file, Span{ b, e }, "math: " + message);
    }
    return doc.ok();
}

std::vector<MathBlock> collect_math(const Document& document, Diagnostics& diagnostics) {
    std::vector<MathBlock> out;
    for (const Node* el : document.elements_by_tag("math")) {
        MathBlock block;
        block.element = el;
        block.source = math_source_of(*el);
        block.syntax = math_syntax_of(*el, document.path(), diagnostics);
        block.display = el->attribute_or("display", "inline") == "block";
        check_math(block, document.path(), diagnostics);
        out.push_back(std::move(block));
    }
    return out;
}

} // namespace grove
