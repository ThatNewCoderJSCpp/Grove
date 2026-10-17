#include "grove/bark/parser.hpp"
#include "grove/common/cursor.hpp"
#include "grove/bark/properties.hpp"
#include "grove/markup/tags.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdlib>

namespace grove {
namespace bark {

namespace {

enum class Tok {
    End,
    Ident,
    Number,
    String,
    Hash,
    Variable,
    AtKeyword,
    LBrace,
    RBrace,
    LParen,
    RParen,
    Colon,
    Semicolon,
    Comma,
    Greater,
    Dot,
    Star,
    Equals,
    Bang,
    Ampersand,
    Invalid
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    double number = 0.0;
    std::string unit;
    bool space_before = false;
    Span span;
};

bool is_ident_start(char c) noexcept { return Cursor::is_alpha(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool is_ident_char(char c) noexcept { return is_ident_start(c) || Cursor::is_digit(c) || c == '-'; }

class Lexer {
public:
    Lexer(std::string_view source, const std::string& file, Diagnostics& diag, const Position& origin) : m_cursor(source, origin), m_file(file), m_diag(diag) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            const bool space = skip_trivia();
            Token t = next();
            t.space_before = space;
            const bool end = t.kind == Tok::End;
            out.push_back(std::move(t));
            if (end) break;
        }
        return out;
    }

private:
    bool skip_trivia() {
        bool any = false;
        for (;;) {
            if (!m_cursor.at_end() && Cursor::is_space(m_cursor.peek())) {
                m_cursor.advance();
                any = true;
            } else if (m_cursor.starts_with("//")) {
                while (!m_cursor.at_end() && m_cursor.peek() != '\n') m_cursor.advance();
                any = true;
            } else if (m_cursor.starts_with("/*")) {
                const Position start = m_cursor.position();
                m_cursor.advance(2);
                while (!m_cursor.at_end() && !m_cursor.starts_with("*/")) m_cursor.advance();
                if (m_cursor.at_end()) m_diag.error(m_file, m_cursor.span_from(start), "comment is never closed with '*/'");
                else m_cursor.advance(2);
                any = true;
            } else {
                return any;
            }
        }
    }

    std::string read_ident() {
        const Position start = m_cursor.position();
        while (!m_cursor.at_end() && is_ident_char(m_cursor.peek())) m_cursor.advance();
        return std::string(m_cursor.slice(start));
    }

    Token make(Tok kind, const Position& start, std::string text = {}) {
        Token t;
        t.kind = kind;
        t.text = std::move(text);
        t.span = m_cursor.span_from(start);
        return t;
    }

    Token next() {
        const Position start = m_cursor.position();
        if (m_cursor.at_end()) return make(Tok::End, start);
        const char c = m_cursor.peek();

        const bool number_start = Cursor::is_digit(c)
            || (c == '.' && Cursor::is_digit(m_cursor.peek(1)))
            || ((c == '-' || c == '+') && (Cursor::is_digit(m_cursor.peek(1)) || (m_cursor.peek(1) == '.' && Cursor::is_digit(m_cursor.peek(2)))));
        if (number_start) return lex_number(start);

        if (is_ident_start(c) || (c == '-' && is_ident_start(m_cursor.peek(1)))) {
            std::string id = read_ident();
            return make(Tok::Ident, start, std::move(id));
        }

        switch (c) {
            case '"':
            case '\'': return lex_string(start, c);
            case '#': { m_cursor.advance(); std::string id = read_ident_or_hex(); return make(Tok::Hash, start, std::move(id)); }
            case '$': { m_cursor.advance(); std::string id = read_ident(); if (id.empty()) m_diag.error(m_file, m_cursor.span_from(start), "'$' must be followed by a variable name"); return make(Tok::Variable, start, std::move(id)); }
            case '@': { m_cursor.advance(); std::string id = read_ident(); return make(Tok::AtKeyword, start, std::move(id)); }
            case '{': m_cursor.advance(); return make(Tok::LBrace, start, "{");
            case '}': m_cursor.advance(); return make(Tok::RBrace, start, "}");
            case '(': m_cursor.advance(); return make(Tok::LParen, start, "(");
            case ')': m_cursor.advance(); return make(Tok::RParen, start, ")");
            case ':': m_cursor.advance(); return make(Tok::Colon, start, ":");
            case ';': m_cursor.advance(); return make(Tok::Semicolon, start, ";");
            case ',': m_cursor.advance(); return make(Tok::Comma, start, ",");
            case '>': m_cursor.advance(); return make(Tok::Greater, start, ">");
            case '.': m_cursor.advance(); return make(Tok::Dot, start, ".");
            case '*': m_cursor.advance(); return make(Tok::Star, start, "*");
            case '=': m_cursor.advance(); return make(Tok::Equals, start, "=");
            case '!': m_cursor.advance(); return make(Tok::Bang, start, "!");
            case '&': m_cursor.advance(); return make(Tok::Ampersand, start, "&");
            default: break;
        }
        m_cursor.advance();
        Token t = make(Tok::Invalid, start, std::string(1, c));
        m_diag.error(m_file, t.span, std::string("unexpected character '") + c + "'");
        return t;
    }

    std::string read_ident_or_hex() {
        const Position start = m_cursor.position();
        while (!m_cursor.at_end() && (is_ident_char(m_cursor.peek()) || Cursor::is_digit(m_cursor.peek()))) m_cursor.advance();
        return std::string(m_cursor.slice(start));
    }

    Token lex_number(const Position& start) {
        if (m_cursor.peek() == '+' || m_cursor.peek() == '-') m_cursor.advance();
        while (Cursor::is_digit(m_cursor.peek())) m_cursor.advance();
        if (m_cursor.peek() == '.' && Cursor::is_digit(m_cursor.peek(1))) {
            m_cursor.advance();
            while (Cursor::is_digit(m_cursor.peek())) m_cursor.advance();
        }
        if ((m_cursor.peek() == 'e' || m_cursor.peek() == 'E') && (Cursor::is_digit(m_cursor.peek(1)) || ((m_cursor.peek(1) == '-' || m_cursor.peek(1) == '+') && Cursor::is_digit(m_cursor.peek(2))))) {
            m_cursor.advance(2);
            while (Cursor::is_digit(m_cursor.peek())) m_cursor.advance();
        }
        const std::string digits(m_cursor.slice(start));
        Token t;
        t.kind = Tok::Number;
        t.number = std::strtod(digits.c_str(), nullptr);
        if (m_cursor.peek() == '%') {
            m_cursor.advance();
            t.unit = "%";
        } else if (is_ident_start(m_cursor.peek())) {
            t.unit = read_ident();
        }
        t.text = std::string(m_cursor.slice(start));
        t.span = m_cursor.span_from(start);
        return t;
    }

    Token lex_string(const Position& start, char quote) {
        m_cursor.advance();
        std::string text;
        while (!m_cursor.at_end() && m_cursor.peek() != quote && m_cursor.peek() != '\n') {
            char c = m_cursor.advance();
            if (c == '\\' && !m_cursor.at_end()) {
                const char e = m_cursor.advance();
                switch (e) {
                    case 'n': text += '\n'; break;
                    case 't': text += '\t'; break;
                    default: text += e; break;
                }
                continue;
            }
            text += c;
        }
        if (m_cursor.peek() != quote) {
            Token t = make(Tok::String, start, std::move(text));
            m_diag.error(m_file, t.span, "string is never closed");
            return t;
        }
        m_cursor.advance();
        return make(Tok::String, start, std::move(text));
    }

    Cursor m_cursor;
    const std::string& m_file;
    Diagnostics& m_diag;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, const std::string& file, Diagnostics& diag)
        : m_tokens(std::move(tokens)), m_file(file), m_diag(diag) {}

    Stylesheet parse_sheet() {
        m_sheet.set_path(m_file);
        while (!at(Tok::End)) {
            if (at(Tok::RBrace)) {
                m_diag.error(m_file, peek().span, "unexpected '}'");
                advance();
                continue;
            }
            parse_item({});
        }
        return std::move(m_sheet);
    }

    std::vector<Declaration> parse_declaration_list() {
        std::vector<Declaration> out;
        while (!at(Tok::End)) {
            if (at(Tok::Semicolon)) { advance(); continue; }
            if (at(Tok::Variable)) { parse_variable(); continue; }
            Declaration d;
            if (parse_declaration(d)) out.push_back(std::move(d));
        }
        return out;
    }

    void set_variables(const std::map<std::string, ValueList>& vars) { m_sheet.variables() = vars; }
    Marks take_marks() { return std::move(m_sheet.marks()); }

    std::optional<std::vector<Selector>> parse_selector_list_only() {
        std::vector<Selector> out;
        do {
            Selector s;
            if (!parse_selector(s, false)) return std::nullopt;
            out.push_back(std::move(s));
        } while (accept(Tok::Comma));
        if (!at(Tok::End)) {
            m_diag.error(m_file, peek().span, "unexpected '" + peek().text + "' in selector");
            return std::nullopt;
        }
        return out;
    }

private:
    const Token& peek(std::size_t ahead = 0) const {
        const std::size_t i = std::min(m_pos + ahead, m_tokens.size() - 1);
        return m_tokens[i];
    }
    bool at(Tok kind) const { return peek().kind == kind; }
    const Token& advance() {
        const Token& t = peek();
        if (m_pos < m_tokens.size() - 1) ++m_pos;
        return t;
    }
    Position prev_end() const {
        return m_pos > 0 ? m_tokens[m_pos - 1].span.end : peek().span.begin;
    }

    void mark(MarkKind kind, const Span& span) { m_sheet.marks().push_back(Mark{ kind, span }); }

    void mark_color(const Span& span, const Rgba& c) {
        Mark m{ MarkKind::Color, span };
        m.r = c.r;
        m.g = c.g;
        m.b = c.b;
        m.a = c.a;
        m_sheet.marks().push_back(m);
    }

    void mark_variable_use(const Token& t, bool resolved) { mark(resolved ? MarkKind::Variable : MarkKind::UnresolvedVariable, t.span); }

    bool accept(Tok kind) {
        if (!at(kind)) return false;
        advance();
        return true;
    }

    void sync_statement() {
        int depth = 0;
        while (!at(Tok::End)) {
            if (at(Tok::LBrace)) ++depth;
            if (at(Tok::RBrace)) {
                if (depth == 0) return;
                --depth;
                if (depth == 0) { advance(); return; }
            }
            if (at(Tok::Semicolon) && depth == 0) { advance(); return; }
            advance();
        }
    }

    void parse_item(const std::vector<Selector>& parents) {
        if (at(Tok::Variable)) { parse_variable(); return; }
        if (at(Tok::AtKeyword)) { parse_at_rule(); return; }
        parse_rule(parents);
    }

    void parse_variable() {
        const Token name = advance();
        mark(MarkKind::VariableDeclaration, name.span);
        if (!accept(Tok::Equals) && !accept(Tok::Colon)) {
            m_diag.error(m_file, peek().span, "expected '=' after $" + name.text);
            sync_statement();
            return;
        }
        const std::size_t errors_before = m_diag.error_count();
        ValueList values = parse_values();
        if (values.empty() && m_diag.error_count() == errors_before) m_diag.error(m_file, name.span, "$" + name.text + " has no value");
        if (!accept(Tok::Semicolon)) m_diag.error(m_file, peek().span, "expected ';' after $" + name.text);
        m_sheet.variables()[name.text] = std::move(values);
    }

    void parse_at_rule() {
        const Token at_kw = advance();
        if (at_kw.text == "import") {
            if (at(Tok::String)) m_sheet.imports().push_back(advance().text);
            else m_diag.error(m_file, peek().span, "@import expects a quoted path");
            if (!accept(Tok::Semicolon)) m_diag.error(m_file, peek().span, "expected ';' after @import");
            return;
        }
        m_diag.error(m_file, at_kw.span, "unknown @-rule '@" + at_kw.text + "'");
        sync_statement();
    }

    bool parse_compound(Compound& c) {
        bool any = false;
        if (at(Tok::Ident)) {
            const Token& t = advance();
            c.tag = t.text;
            any = true;
            const bool known = is_known_tag(c.tag) || c.tag.find('-') != std::string::npos;
            mark(known ? MarkKind::SelectorTag : MarkKind::UnknownSelectorTag, t.span);
            if (!known) m_diag.warning(m_file, t.span, "there is no <" + c.tag + "> element; custom elements need a '-' in their name");
        }
        else if (at(Tok::Star)) { advance(); c.tag = "*"; any = true; }
        for (;;) {
            if (peek().space_before && any) break;
            if (at(Tok::Hash)) {
                const Token& t = advance();
                if (!c.id.empty()) m_diag.error(m_file, t.span, "a selector part can only have one #id");
                c.id = t.text;
                any = true;
            } else if (at(Tok::Dot)) {
                const Token& dot = advance();
                if (!at(Tok::Ident) || peek().space_before) {
                    m_diag.error(m_file, dot.span, "expected a class name after '.'");
                    return false;
                }
                c.classes.push_back(advance().text);
                any = true;
            } else if (at(Tok::Colon) && peek(1).kind == Tok::Ident && !peek(1).space_before) {
                advance();
                const Token& name = advance();
                if (!is_known_pseudo_class(name.text)) m_diag.warning(m_file, name.span, "unknown pseudo-class ':" + name.text + "'; it never matches");
                c.pseudos.push_back(name.text);
                any = true;
            } else {
                break;
            }
        }
        return any;
    }

    bool parse_selector(Selector& sel, bool nested) {
        const Span start = peek().span;
        Combinator next = Combinator::None;
        if (nested) {
            next = Combinator::Descendant;
            if (accept(Tok::Ampersand)) next = Combinator::None;
            if (accept(Tok::Greater)) next = Combinator::Child;
        }
        for (;;) {
            Compound c;
            if (!parse_compound(c)) {
                m_diag.error(m_file, peek().span, "expected a selector");
                return false;
            }
            c.combinator = sel.parts.empty() && !nested ? Combinator::None : next;
            sel.parts.push_back(std::move(c));
            if (at(Tok::Comma) || at(Tok::LBrace) || at(Tok::End)) break;
            if (accept(Tok::Greater)) next = Combinator::Child;
            else next = Combinator::Descendant;
        }
        sel.span = Span{ start.begin, peek().span.begin };
        return true;
    }

    static std::vector<Selector> combine(const std::vector<Selector>& parents, const std::vector<Selector>& children) {
        if (parents.empty()) return children;
        std::vector<Selector> out;
        for (const Selector& p : parents) {
            for (const Selector& c : children) {
                Selector s = p;
                if (!c.parts.empty() && c.parts.front().combinator == Combinator::None && !s.parts.empty()) {
                    Compound merged = s.parts.back();
                    const Compound& first = c.parts.front();
                    if (!first.tag.empty()) merged.tag = first.tag;
                    if (!first.id.empty()) merged.id = first.id;
                    merged.classes.insert(merged.classes.end(), first.classes.begin(), first.classes.end());
                    merged.pseudos.insert(merged.pseudos.end(), first.pseudos.begin(), first.pseudos.end());
                    s.parts.back() = merged;
                    s.parts.insert(s.parts.end(), c.parts.begin() + 1, c.parts.end());
                } else {
                    s.parts.insert(s.parts.end(), c.parts.begin(), c.parts.end());
                }
                s.span = c.span;
                out.push_back(std::move(s));
            }
        }
        return out;
    }

    void parse_rule(const std::vector<Selector>& parents) {
        const Span start = peek().span;
        std::vector<Selector> selectors;
        do {
            Selector s;
            if (!parse_selector(s, !parents.empty())) {
                sync_statement();
                return;
            }
            selectors.push_back(std::move(s));
        } while (accept(Tok::Comma));

        if (!accept(Tok::LBrace)) {
            m_diag.error(m_file, peek().span, "expected '{' after selector");
            sync_statement();
            return;
        }

        Rule rule;
        rule.selectors = combine(parents, selectors);
        rule.span = start;
        const std::size_t index = m_sheet.rules().size();
        m_sheet.rules().push_back(rule);

        std::vector<Declaration> decls;
        while (!at(Tok::RBrace) && !at(Tok::End)) {
            if (accept(Tok::Semicolon)) continue;
            if (at(Tok::Variable)) { parse_variable(); continue; }
            if (looks_like_nested_rule()) {
                parse_rule(rule.selectors);
                continue;
            }
            Declaration d;
            if (parse_declaration(d)) decls.push_back(std::move(d));
        }
        if (!accept(Tok::RBrace)) m_diag.error(m_file, start, "rule is never closed with '}'");
        m_sheet.rules()[index].declarations = std::move(decls);
    }

    bool looks_like_nested_rule() const {
        if (at(Tok::Ampersand) || at(Tok::Greater) || at(Tok::Dot) || at(Tok::Hash) || at(Tok::Star)) return true;
        for (std::size_t i = 0; m_pos + i < m_tokens.size(); ++i) {
            const Tok k = peek(i).kind;
            if (k == Tok::LBrace) return true;
            if (k == Tok::Semicolon || k == Tok::RBrace || k == Tok::End) return false;
        }
        return false;
    }

    bool parse_declaration(Declaration& d) {
        if (!at(Tok::Ident)) {
            m_diag.error(m_file, peek().span, "expected a property name");
            sync_statement();
            return false;
        }
        const Token name = advance();
        d.property = name.text;
        d.span = name.span;
        if (is_known_property(name.text)) {
            mark(MarkKind::Property, name.span);
        } else {
            mark(MarkKind::UnknownProperty, name.span);
            m_diag.warning(m_file, name.span, "unknown property '" + name.text + "'");
        }
        if (!accept(Tok::Colon)) {
            m_diag.error(m_file, peek().span, "expected ':' after '" + name.text + "'");
            sync_statement();
            return false;
        }
        const std::size_t errors_before = m_diag.error_count();
        d.values = parse_values();
        if (accept(Tok::Bang)) {
            if (at(Tok::Ident) && peek().text == "important") { advance(); d.important = true; }
            else m_diag.error(m_file, peek().span, "expected 'important' after '!'");
        }
        if (d.values.empty() && m_diag.error_count() == errors_before) m_diag.error(m_file, name.span, "'" + name.text + "' has no value");
        if (!accept(Tok::Semicolon) && !at(Tok::RBrace) && !at(Tok::End)) {
            m_diag.error(m_file, peek().span, "expected ';' after the value of '" + name.text + "'");
            sync_statement();
        }
        return !d.values.empty();
    }

    ValueList parse_values() {
        ValueList out;
        while (!at(Tok::Semicolon) && !at(Tok::RBrace) && !at(Tok::End) && !at(Tok::Bang) && !at(Tok::LBrace)) {
            if (accept(Tok::Comma)) continue;
            if (at(Tok::Variable)) {
                const Token& t = advance();
                auto it = m_sheet.variables().find(t.text);
                mark_variable_use(t, it != m_sheet.variables().end());
                if (it == m_sheet.variables().end()) {
                    m_diag.error(m_file, t.span, "unknown variable $" + t.text);
                    continue;
                }
                out.insert(out.end(), it->second.begin(), it->second.end());
                continue;
            }
            Value v;
            if (parse_value(v)) out.push_back(std::move(v));
        }
        return out;
    }

    static std::uint8_t channel(const Value& v) {
        double n = v.number;
        if (v.unit == "%") n = n * 255.0 / 100.0;
        return static_cast<std::uint8_t>(std::clamp(std::lround(n), 0L, 255L));
    }

    static std::uint8_t alpha_channel(const Value& v) {
        double n = v.unit == "%" ? v.number / 100.0 : v.number;
        if (n > 1.0) return static_cast<std::uint8_t>(std::clamp(std::lround(n), 0L, 255L));
        return static_cast<std::uint8_t>(std::clamp(std::lround(n * 255.0), 0L, 255L));
    }

    static double hue_to_rgb(double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6) return p + (q - p) * 6 * t;
        if (t < 1.0 / 2) return q;
        if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
        return p;
    }

    bool evaluate_function(Value& v) {
        const auto all_numbers = std::all_of(v.args.begin(), v.args.end(), [](const Value& a) { return a.is_number(); });
        if ((v.text == "rgb" || v.text == "rgba") && all_numbers && (v.args.size() == 3 || v.args.size() == 4)) {
            Rgba c{ channel(v.args[0]), channel(v.args[1]), channel(v.args[2]), 255 };
            if (v.args.size() == 4) c.a = alpha_channel(v.args[3]);
            const Span span = v.span;
            v = Value::make_color(c);
            v.span = span;
            return true;
        }
        if ((v.text == "hsl" || v.text == "hsla") && all_numbers && (v.args.size() == 3 || v.args.size() == 4)) {
            const double h = std::fmod(std::fmod(v.args[0].number, 360.0) + 360.0, 360.0) / 360.0;
            const double s = std::clamp(v.args[1].number / 100.0, 0.0, 1.0);
            const double l = std::clamp(v.args[2].number / 100.0, 0.0, 1.0);
            double r = l, g = l, b = l;
            if (s > 0) {
                const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
                const double p = 2 * l - q;
                r = hue_to_rgb(p, q, h + 1.0 / 3);
                g = hue_to_rgb(p, q, h);
                b = hue_to_rgb(p, q, h - 1.0 / 3);
            }
            Rgba c{ static_cast<std::uint8_t>(std::lround(r * 255)), static_cast<std::uint8_t>(std::lround(g * 255)), static_cast<std::uint8_t>(std::lround(b * 255)), 255 };
            if (v.args.size() == 4) c.a = alpha_channel(v.args[3]);
            const Span span = v.span;
            v = Value::make_color(c);
            v.span = span;
            return true;
        }
        if (v.text == "rgb" || v.text == "rgba" || v.text == "hsl" || v.text == "hsla") {
            m_diag.error(m_file, v.span, v.text + "() expects 3 or 4 numbers");
            return false;
        }
        return true;
    }

    bool parse_value(Value& v) {
        const Token& t = advance();
        v.span = t.span;
        switch (t.kind) {
            case Tok::Number:
                v.kind = ValueKind::Number;
                v.number = t.number;
                v.unit = t.unit;
                return true;
            case Tok::String:
                v.kind = ValueKind::String;
                v.text = t.text;
                return true;
            case Tok::Hash: {
                auto c = parse_hex_color(t.text);
                if (!c) {
                    m_diag.error(m_file, t.span, "'#" + t.text + "' is not a valid color");
                    return false;
                }
                v.kind = ValueKind::Color;
                v.color = *c;
                mark_color(t.span, *c);
                return true;
            }
            case Tok::Ident:
                if (at(Tok::LParen) && !peek().space_before) {
                    advance();
                    v.kind = ValueKind::Function;
                    v.text = t.text;
                    while (!at(Tok::RParen) && !at(Tok::End) && !at(Tok::Semicolon) && !at(Tok::RBrace)) {
                        if (accept(Tok::Comma)) continue;
                        if (at(Tok::Variable)) {
                            const Token& var = advance();
                            auto it = m_sheet.variables().find(var.text);
                            mark_variable_use(var, it != m_sheet.variables().end());
                            if (it == m_sheet.variables().end()) m_diag.error(m_file, var.span, "unknown variable $" + var.text);
                            else v.args.insert(v.args.end(), it->second.begin(), it->second.end());
                            continue;
                        }
                        Value arg;
                        if (parse_value(arg)) v.args.push_back(std::move(arg));
                    }
                    if (!accept(Tok::RParen)) m_diag.error(m_file, t.span, t.text + "( is never closed with ')'");
                    v.span = Span{ t.span.begin, prev_end() };
                    const bool ok = evaluate_function(v);
                    if (ok && v.is_color()) mark_color(v.span, v.color);
                    return ok;
                }
                v.kind = ValueKind::Ident;
                v.text = t.text;
                if (const auto named = named_color(t.text)) mark_color(t.span, *named);
                return true;
            default:
                m_diag.error(m_file, t.span, "unexpected '" + t.text + "' in a value");
                return false;
        }
    }

    std::vector<Token> m_tokens;
    std::size_t m_pos = 0;
    const std::string& m_file;
    Diagnostics& m_diag;
    Stylesheet m_sheet;
};

} // namespace

Stylesheet parse_stylesheet(std::string_view source, const std::string& file, Diagnostics& diagnostics, const Position& origin, const std::map<std::string, ValueList>& variables) {
    Parser parser(Lexer(source, file, diagnostics, origin).run(), file, diagnostics);
    parser.set_variables(variables);
    return parser.parse_sheet();
}

std::vector<Declaration> parse_declarations(std::string_view source, const std::string& file, Diagnostics& diagnostics, const std::map<std::string, ValueList>& variables, const Position& origin) {
    Parser parser(Lexer(source, file, diagnostics, origin).run(), file, diagnostics);
    parser.set_variables(variables);
    return parser.parse_declaration_list();
}

std::optional<std::vector<Selector>> parse_selectors(std::string_view source, Diagnostics& diagnostics) {
    const std::string file = "<selector>";
    Diagnostics local;
    Parser parser(Lexer(source, file, local, Position{}).run(), file, local);
    std::optional<std::vector<Selector>> out = parser.parse_selector_list_only();
    for (const Diagnostic& d : local.items()) {
        if (d.severity == Severity::Error) diagnostics.add(d.severity, d.file, d.span, d.message);
    }
    if (!local.ok()) return std::nullopt;
    return out;
}

std::vector<Declaration> parse_declarations(std::string_view source, const std::string& file, Diagnostics& diagnostics, const std::map<std::string, ValueList>& variables, const Position& origin, Marks& marks) {
    Parser parser(Lexer(source, file, diagnostics, origin).run(), file, diagnostics);
    parser.set_variables(variables);
    std::vector<Declaration> out = parser.parse_declaration_list();
    Marks found = parser.take_marks();
    marks.insert(marks.end(), found.begin(), found.end());
    return out;
}

} // namespace bark
} // namespace grove
