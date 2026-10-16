#include "grove/markup/parser.hpp"
#include "grove/markup/tags.hpp"
#include "grove/common/cursor.hpp"

#include <algorithm>
#include <cstdint>

namespace grove {

namespace {

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool is_name_start(char c) noexcept { return Cursor::is_alpha(c) || c == '_'; }
bool is_name_char(char c) noexcept { return Cursor::is_alpha(c) || Cursor::is_digit(c) || c == '_' || c == '-' || c == ':' || c == '.'; }
bool is_attr_char(char c) noexcept { return is_name_char(c) || c == '@'; }

bool only_whitespace(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), [](char c) { return Cursor::is_space(c); });
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = Cursor::lower(c);
    return out;
}

class MarkupParser {
public:
    MarkupParser(std::string_view source, const std::string& file, Diagnostics& diagnostics, const MarkupOptions& options)
        : m_cursor(source), m_file(file), m_diag(diagnostics), m_options(options) {}

    Document run() {
        m_doc.set_path(m_file);
        m_stack.push_back(&m_doc.root());
        while (!m_cursor.at_end()) {
            if (m_cursor.starts_with("<!--")) parse_comment();
            else if (m_cursor.starts_with("</")) parse_close_tag();
            else if (m_cursor.peek() == '<' && is_name_start(m_cursor.peek(1))) parse_open_tag();
            else if (m_cursor.starts_with("<!") || m_cursor.starts_with("<?")) parse_declaration();
            else parse_text();
        }
        for (std::size_t i = m_stack.size(); i-- > 1;) {
            m_diag.error(m_file, m_stack[i]->span(), "<" + m_stack[i]->tag() + "> is never closed");
        }
        return std::move(m_doc);
    }

private:
    Node& top() { return *m_stack.back(); }

    void parse_comment() {
        const Position start = m_cursor.position();
        m_cursor.advance(4);
        while (!m_cursor.at_end() && !m_cursor.starts_with("-->")) m_cursor.advance();
        if (m_cursor.at_end()) {
            m_diag.error(m_file, m_cursor.span_from(start), "comment is never closed with '-->'");
            return;
        }
        m_cursor.advance(3);
    }

    void parse_declaration() {
        const Position start = m_cursor.position();
        while (!m_cursor.at_end() && m_cursor.peek() != '>') m_cursor.advance();
        if (m_cursor.at_end()) {
            m_diag.error(m_file, m_cursor.span_from(start), "declaration is never closed with '>'");
            return;
        }
        m_cursor.advance();
    }

    void parse_text() {
        const Position start = m_cursor.position();
        m_cursor.advance();
        while (!m_cursor.at_end()) {
            if (m_cursor.peek() == '<') {
                const char n = m_cursor.peek(1);
                if (is_name_start(n) || n == '/' || n == '!' || n == '?') break;
            }
            m_cursor.advance();
        }
        const std::string_view raw = m_cursor.slice(start);
        if (!m_options.keep_whitespace_text && only_whitespace(raw)) return;
        top().append_child(Node::make_text(decode_entities(raw), m_cursor.span_from(start)));
    }

    std::string read_name() {
        const Position start = m_cursor.position();
        while (!m_cursor.at_end() && is_name_char(m_cursor.peek())) m_cursor.advance();
        return to_lower(m_cursor.slice(start));
    }

    bool parse_attribute(Node& element) {
        const Position start = m_cursor.position();
        while (!m_cursor.at_end() && is_attr_char(m_cursor.peek())) m_cursor.advance();
        Attribute attr;
        attr.name = std::string(m_cursor.slice(start));
        if (attr.name.empty()) {
            m_diag.error(m_file, Span{ start, start }, std::string("unexpected '") + m_cursor.peek() + "' in tag");
            m_cursor.advance();
            return false;
        }
        m_cursor.skip_whitespace();
        if (m_cursor.peek() == '=') {
            m_cursor.advance();
            m_cursor.skip_whitespace();
            attr.has_value = true;
            const char q = m_cursor.peek();
            if (q == '"' || q == '\'') {
                m_cursor.advance();
                const Position vstart = m_cursor.position();
                while (!m_cursor.at_end() && m_cursor.peek() != q) m_cursor.advance();
                attr.value = decode_entities(m_cursor.slice(vstart));
                if (m_cursor.at_end()) m_diag.error(m_file, m_cursor.span_from(start), "attribute value is never closed");
                else m_cursor.advance();
            } else {
                const Position vstart = m_cursor.position();
                while (!m_cursor.at_end() && !Cursor::is_space(m_cursor.peek()) && m_cursor.peek() != '>' && !m_cursor.starts_with("/>")) m_cursor.advance();
                attr.value = decode_entities(m_cursor.slice(vstart));
                if (attr.value.empty()) m_diag.error(m_file, m_cursor.span_from(start), "attribute '" + attr.name + "' is missing its value after '='");
            }
        }
        attr.span = m_cursor.span_from(start);
        if (element.has_attribute(attr.name)) {
            m_diag.warning(m_file, attr.span, "duplicate attribute '" + attr.name + "' is ignored");
            return true;
        }
        element.add_attribute(std::move(attr));
        return true;
    }

    void parse_open_tag() {
        const Position start = m_cursor.position();
        m_cursor.advance();
        std::string name = read_name();
        auto element = Node::make_element(name, Span{ start, start });
        bool self_closing = false;
        bool closed = false;
        while (!m_cursor.at_end()) {
            m_cursor.skip_whitespace();
            if (m_cursor.match("/>")) { self_closing = true; closed = true; break; }
            if (m_cursor.match(">")) { closed = true; break; }
            if (m_cursor.peek() == '<') break;
            parse_attribute(*element);
        }
        const Span span = m_cursor.span_from(start);
        if (!closed) m_diag.error(m_file, span, "<" + name + " is missing its closing '>'");

        element->set_span(span);
        Node* node = top().append_child(std::move(element));

        if (m_options.warn_unknown_tags && !is_known_tag(name) && name.find('-') == std::string::npos) {
            m_diag.warning(m_file, span, "unknown tag <" + name + ">; custom tags should contain a '-'");
        }

        const TagContent content = tag_content(name);
        if (self_closing || content == TagContent::Void || !closed) return;
        if (content == TagContent::Raw) {
            parse_raw_content(*node);
            return;
        }
        m_stack.push_back(node);
    }

    void parse_raw_content(Node& element) {
        const Position start = m_cursor.position();
        const std::string closer = "</" + element.tag();
        while (!m_cursor.at_end()) {
            if (m_cursor.starts_with_icase(closer)) {
                const char after = m_cursor.peek(closer.size());
                if (after == '>' || Cursor::is_space(after)) break;
            }
            m_cursor.advance();
        }
        element.append_child(Node::make_raw(std::string(m_cursor.slice(start)), m_cursor.span_from(start)));
        if (m_cursor.at_end()) {
            m_diag.error(m_file, element.span(), "<" + element.tag() + "> is never closed");
            return;
        }
        m_cursor.advance(closer.size());
        m_cursor.skip_whitespace();
        if (!m_cursor.match(">")) m_diag.error(m_file, m_cursor.span_from(m_cursor.position()), "expected '>' to finish </" + element.tag() + ">");
    }

    void parse_close_tag() {
        const Position start = m_cursor.position();
        m_cursor.advance(2);
        const std::string name = read_name();
        m_cursor.skip_whitespace();
        if (!m_cursor.match(">")) m_diag.error(m_file, m_cursor.span_from(start), "expected '>' to finish </" + name + ">");
        const Span span = m_cursor.span_from(start);

        if (name.empty()) {
            m_diag.error(m_file, span, "closing tag has no name");
            return;
        }
        for (std::size_t i = m_stack.size(); i-- > 1;) {
            if (m_stack[i]->tag() != name) continue;
            for (std::size_t j = m_stack.size(); j-- > i + 1;) {
                m_diag.error(m_file, m_stack[j]->span(), "<" + m_stack[j]->tag() + "> is not closed before </" + name + ">");
            }
            m_stack.resize(i);
            return;
        }
        if (tag_content(name) == TagContent::Void) {
            m_diag.warning(m_file, span, "<" + name + "> never takes a closing tag; </" + name + "> is ignored");
            return;
        }
        m_diag.error(m_file, span, "</" + name + "> has no matching <" + name + ">");
    }

    Cursor m_cursor;
    const std::string& m_file;
    Diagnostics& m_diag;
    const MarkupOptions& m_options;
    Document m_doc;
    std::vector<Node*> m_stack;
};

} // namespace

std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += '&';
            continue;
        }
        const std::string_view name = text.substr(i + 1, semi - i - 1);
        bool ok = true;
        if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "amp") out += '&';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name == "nbsp") append_utf8(out, 0xA0);
        else if (name.size() > 1 && name[0] == '#') {
            std::uint32_t cp = 0;
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string_view digits = name.substr(hex ? 2 : 1);
            if (digits.empty()) ok = false;
            for (char c : digits) {
                if (hex && Cursor::is_hex(c)) cp = cp * 16 + static_cast<std::uint32_t>(Cursor::is_digit(c) ? c - '0' : Cursor::lower(c) - 'a' + 10);
                else if (!hex && Cursor::is_digit(c)) cp = cp * 10 + static_cast<std::uint32_t>(c - '0');
                else { ok = false; break; }
                if (cp > 0x10FFFF) { ok = false; break; }
            }
            if (ok) append_utf8(out, cp);
        } else {
            ok = false;
        }
        if (!ok) {
            out += '&';
            continue;
        }
        i = semi;
    }
    return out;
}

std::string dedent(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    while (!lines.empty() && only_whitespace(lines.front())) lines.erase(lines.begin());
    while (!lines.empty() && only_whitespace(lines.back())) lines.pop_back();

    std::size_t common = std::string_view::npos;
    for (std::string_view line : lines) {
        if (only_whitespace(line)) continue;
        std::size_t n = 0;
        while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
        common = std::min(common, n);
    }
    if (common == std::string_view::npos) common = 0;

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        if (lines[i].size() > common) out += lines[i].substr(common);
    }
    return out;
}

Document parse_markup(std::string_view source, const std::string& file, Diagnostics& diagnostics, const MarkupOptions& options) {
    return MarkupParser(source, file, diagnostics, options).run();
}

} // namespace grove
