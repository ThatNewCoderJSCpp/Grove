#include "grove/input/code_buffer.hpp"

#include <algorithm>

namespace grove {

namespace {

constexpr std::size_t k_undo_limit = 500;

bool continuation(char c) noexcept { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

bool word_char(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool space_char(char c) noexcept { return c == ' ' || c == '\t'; }

std::size_t code_points_before(const std::string& line, std::size_t column) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < column && i < line.size(); ++i) {
        if (!continuation(line[i])) ++n;
    }
    return n;
}

std::size_t byte_for_code_point(const std::string& line, std::size_t points) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (continuation(line[i])) continue;
        if (n == points) return i;
        ++n;
    }
    return line.size();
}

std::size_t leading_whitespace(const std::string& line) noexcept {
    std::size_t i = 0;
    while (i < line.size() && space_char(line[i])) ++i;
    return i;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out(1);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.emplace_back();
        } else if (c == '\n') {
            out.emplace_back();
        } else {
            out.back() += c;
        }
    }
    return out;
}

} // namespace

std::size_t code_display_column(std::string_view line, std::size_t byte_column, std::size_t tab_size) {
    if (tab_size == 0) tab_size = 4;
    std::size_t col = 0;
    for (std::size_t i = 0; i < byte_column && i < line.size(); ++i) {
        if (continuation(line[i])) continue;
        if (line[i] == '\t') col += tab_size - col % tab_size;
        else ++col;
    }
    return col;
}

std::size_t code_byte_column(std::string_view line, std::size_t display_column, std::size_t tab_size) {
    if (tab_size == 0) tab_size = 4;
    std::size_t col = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (continuation(line[i])) continue;
        const std::size_t width = line[i] == '\t' ? tab_size - col % tab_size : 1;
        if (2 * display_column < 2 * col + width) return i;
        col += width;
    }
    return line.size();
}

std::string code_expand_tabs(std::string_view line, std::size_t tab_size) {
    if (tab_size == 0) tab_size = 4;
    std::string out;
    std::size_t col = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\t') {
            const std::size_t n = tab_size - col % tab_size;
            out.append(n, ' ');
            col += n;
            continue;
        }
        out += c;
        if (!continuation(c)) ++col;
    }
    return out;
}

CodeBuffer::CodeBuffer() : m_lines(1) {}

CodeBuffer::CodeBuffer(std::string_view text) : m_lines(split_lines(text)) {}

void CodeBuffer::set_text(std::string_view text) {
    m_lines = split_lines(text);
    m_caret = m_anchor = CodePos{};
    m_undo.clear();
    m_redo.clear();
    m_last_edit = EditKind::None;
    forget_preferred();
    changed();
}

std::string CodeBuffer::text() const {
    std::string out;
    for (std::size_t i = 0; i < m_lines.size(); ++i) {
        if (i) out += '\n';
        out += m_lines[i];
    }
    return out;
}

std::pair<CodePos, CodePos> CodeBuffer::selection() const noexcept {
    return m_anchor < m_caret ? std::make_pair(m_anchor, m_caret) : std::make_pair(m_caret, m_anchor);
}

std::string CodeBuffer::selected_text() const {
    const auto [a, b] = selection();
    return text_between(a, b);
}

std::string CodeBuffer::text_between(CodePos a, CodePos b) const {
    a = clamp(a);
    b = clamp(b);
    if (b < a) std::swap(a, b);
    if (a.line == b.line) return m_lines[a.line].substr(a.column, b.column - a.column);
    std::string out = m_lines[a.line].substr(a.column);
    for (std::size_t l = a.line + 1; l < b.line; ++l) out += '\n' + m_lines[l];
    out += '\n' + m_lines[b.line].substr(0, b.column);
    return out;
}

CodePos CodeBuffer::clamp(CodePos p) const noexcept {
    if (p.line >= m_lines.size()) {
        p.line = m_lines.size() - 1;
        p.column = m_lines[p.line].size();
    }
    const std::string& l = m_lines[p.line];
    if (p.column > l.size()) p.column = l.size();
    while (p.column > 0 && p.column < l.size() && continuation(l[p.column])) --p.column;
    return p;
}

void CodeBuffer::set_caret(CodePos p, bool extend) {
    m_caret = clamp(p);
    if (!extend) m_anchor = m_caret;
    forget_preferred();
    m_last_edit = EditKind::None;
}

void CodeBuffer::select(CodePos anchor, CodePos caret) {
    m_anchor = clamp(anchor);
    m_caret = clamp(caret);
    forget_preferred();
    m_last_edit = EditKind::None;
}

void CodeBuffer::select_all() {
    m_anchor = CodePos{};
    m_caret = CodePos{ m_lines.size() - 1, m_lines.back().size() };
    forget_preferred();
}

void CodeBuffer::select_word_at(CodePos p) {
    p = clamp(p);
    const std::string& l = m_lines[p.line];
    std::size_t a = p.column;
    std::size_t b = p.column;
    if (a < l.size() && word_char(l[a])) {
        while (a > 0 && word_char(l[a - 1])) --a;
        while (b < l.size() && word_char(l[b])) ++b;
    } else if (a > 0 && word_char(l[a - 1])) {
        while (a > 0 && word_char(l[a - 1])) --a;
    } else if (b < l.size()) {
        b = step_right(p).column;
    }
    select(CodePos{ p.line, a }, CodePos{ p.line, b });
}

void CodeBuffer::select_line(std::size_t line) {
    if (line >= m_lines.size()) line = m_lines.size() - 1;
    if (line + 1 < m_lines.size()) select(CodePos{ line, 0 }, CodePos{ line + 1, 0 });
    else select(CodePos{ line, 0 }, CodePos{ line, m_lines[line].size() });
}

CodePos CodeBuffer::step_left(CodePos p) const noexcept {
    if (p.column == 0) {
        if (p.line == 0) return p;
        return CodePos{ p.line - 1, m_lines[p.line - 1].size() };
    }
    const std::string& l = m_lines[p.line];
    std::size_t c = p.column - 1;
    while (c > 0 && continuation(l[c])) --c;
    return CodePos{ p.line, c };
}

CodePos CodeBuffer::step_right(CodePos p) const noexcept {
    const std::string& l = m_lines[p.line];
    if (p.column >= l.size()) {
        if (p.line + 1 >= m_lines.size()) return p;
        return CodePos{ p.line + 1, 0 };
    }
    std::size_t c = p.column + 1;
    while (c < l.size() && continuation(l[c])) ++c;
    return CodePos{ p.line, c };
}

CodePos CodeBuffer::word_left(CodePos p) const noexcept {
    if (p.column == 0) return step_left(p);
    const std::string& l = m_lines[p.line];
    std::size_t c = p.column;
    while (c > 0 && space_char(l[c - 1])) --c;
    if (c > 0 && word_char(l[c - 1])) {
        while (c > 0 && word_char(l[c - 1])) --c;
    } else if (c > 0) {
        while (c > 0 && !word_char(l[c - 1]) && !space_char(l[c - 1])) --c;
    }
    return CodePos{ p.line, c };
}

CodePos CodeBuffer::word_right(CodePos p) const noexcept {
    const std::string& l = m_lines[p.line];
    if (p.column >= l.size()) return step_right(p);
    std::size_t c = p.column;
    if (word_char(l[c])) {
        while (c < l.size() && word_char(l[c])) ++c;
    } else if (!space_char(l[c])) {
        while (c < l.size() && !word_char(l[c]) && !space_char(l[c])) ++c;
    }
    while (c < l.size() && space_char(l[c])) ++c;
    return CodePos{ p.line, c };
}

void CodeBuffer::move_left(bool extend, bool word) {
    if (has_selection() && !extend) set_caret(selection().first);
    else set_caret(word ? word_left(m_caret) : step_left(m_caret), extend);
}

void CodeBuffer::move_right(bool extend, bool word) {
    if (has_selection() && !extend) set_caret(selection().second);
    else set_caret(word ? word_right(m_caret) : step_right(m_caret), extend);
}

void CodeBuffer::move_up(bool extend, std::size_t lines) {
    const std::size_t want = m_has_preferred ? m_preferred : code_points_before(m_lines[m_caret.line], m_caret.column);
    CodePos p = m_caret;
    if (p.line == 0) p.column = 0;
    else {
        p.line = p.line >= lines ? p.line - lines : 0;
        p.column = byte_for_code_point(m_lines[p.line], want);
    }
    set_caret(p, extend);
    m_preferred = want;
    m_has_preferred = true;
}

void CodeBuffer::move_down(bool extend, std::size_t lines) {
    const std::size_t want = m_has_preferred ? m_preferred : code_points_before(m_lines[m_caret.line], m_caret.column);
    CodePos p = m_caret;
    if (p.line + 1 >= m_lines.size()) p.column = m_lines[p.line].size();
    else {
        p.line = std::min(m_lines.size() - 1, p.line + lines);
        p.column = byte_for_code_point(m_lines[p.line], want);
    }
    set_caret(p, extend);
    m_preferred = want;
    m_has_preferred = true;
}

void CodeBuffer::move_home(bool extend) {
    const std::size_t indent = leading_whitespace(m_lines[m_caret.line]);
    const std::size_t target = m_caret.column == indent ? 0 : indent;
    set_caret(CodePos{ m_caret.line, target }, extend);
}

void CodeBuffer::move_end(bool extend) { set_caret(CodePos{ m_caret.line, m_lines[m_caret.line].size() }, extend); }

void CodeBuffer::move_start(bool extend) { set_caret(CodePos{}, extend); }

void CodeBuffer::move_finish(bool extend) { set_caret(CodePos{ m_lines.size() - 1, m_lines.back().size() }, extend); }

void CodeBuffer::remember(EditKind kind) {
    const bool coalesce = kind != EditKind::Other && kind == m_last_edit && !m_undo.empty();
    if (!coalesce) {
        m_undo.push_back(Snapshot{ m_lines, m_caret, m_anchor });
        if (m_undo.size() > k_undo_limit) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_last_edit = kind;
}

void CodeBuffer::changed() { ++m_revision; }

void CodeBuffer::erase_range(CodePos a, CodePos b) {
    if (b < a) std::swap(a, b);
    if (a == b) return;
    if (a.line == b.line) {
        m_lines[a.line].erase(a.column, b.column - a.column);
    } else {
        m_lines[a.line] = m_lines[a.line].substr(0, a.column) + m_lines[b.line].substr(b.column);
        m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(a.line) + 1, m_lines.begin() + static_cast<std::ptrdiff_t>(b.line) + 1);
    }
}

CodePos CodeBuffer::insert_at(CodePos p, std::string_view text) {
    const std::vector<std::string> parts = split_lines(text);
    std::string& line = m_lines[p.line];
    const std::string tail = line.substr(p.column);
    line = line.substr(0, p.column) + parts.front();
    if (parts.size() == 1) {
        line += tail;
        return CodePos{ p.line, p.column + parts.front().size() };
    }
    std::vector<std::string> added(parts.begin() + 1, parts.end());
    const std::size_t end_column = added.back().size();
    added.back() += tail;
    m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(p.line) + 1, added.begin(), added.end());
    return CodePos{ p.line + added.size(), end_column };
}

void CodeBuffer::insert(std::string_view text) {
    const bool typing = text.size() <= 4 && text.find('\n') == std::string_view::npos && !(text.size() == 1 && space_char(text[0]));
    remember(typing && !has_selection() ? EditKind::Typing : EditKind::Other);
    if (has_selection()) {
        const auto [a, b] = selection();
        erase_range(a, b);
        m_caret = a;
    }
    m_caret = m_anchor = insert_at(m_caret, text);
    forget_preferred();
    changed();
    if (!typing) m_last_edit = EditKind::None;
}

void CodeBuffer::new_line(std::size_t tab_size, bool spaces) {
    remember(EditKind::Other);
    if (has_selection()) {
        const auto [a, b] = selection();
        erase_range(a, b);
        m_caret = a;
    }
    const std::string& current = m_lines[m_caret.line];
    std::string indent = current.substr(0, std::min(leading_whitespace(current), m_caret.column));
    std::size_t before = m_caret.column;
    while (before > 0 && space_char(current[before - 1])) --before;
    const char opener = before > 0 ? current[before - 1] : '\0';
    std::size_t after = m_caret.column;
    while (after < current.size() && space_char(current[after])) ++after;
    const char closer = after < current.size() ? current[after] : '\0';
    const bool opens = opener == '{' || opener == '(' || opener == '[';
    const bool closes = (opener == '{' && closer == '}') || (opener == '(' && closer == ')') || (opener == '[' && closer == ']');
    const std::string unit = spaces ? std::string(tab_size ? tab_size : 4, ' ') : std::string("\t");
    std::string text = "\n" + indent + (opens ? unit : std::string());
    const std::size_t caret_offset = text.size();
    if (closes) text += "\n" + indent;
    m_lines[m_caret.line].erase(m_caret.column, after - m_caret.column);
    const CodePos start = m_caret;
    insert_at(start, text);
    m_caret = m_anchor = CodePos{ start.line + 1, caret_offset - 1 };
    forget_preferred();
    m_last_edit = EditKind::None;
    changed();
}

void CodeBuffer::delete_selection() {
    if (!has_selection()) return;
    remember(EditKind::Other);
    const auto [a, b] = selection();
    erase_range(a, b);
    m_caret = m_anchor = a;
    forget_preferred();
    m_last_edit = EditKind::None;
    changed();
}

void CodeBuffer::backspace(bool word) {
    if (has_selection()) {
        delete_selection();
        return;
    }
    if (m_caret == CodePos{}) return;
    CodePos from = word ? word_left(m_caret) : step_left(m_caret);
    if (!word && m_caret.column > 0) {
        const std::string& l = m_lines[m_caret.line];
        const char left = l[m_caret.column - 1];
        const char right = m_caret.column < l.size() ? l[m_caret.column] : '\0';
        if ((left == '(' && right == ')') || (left == '[' && right == ']') || (left == '{' && right == '}') || (left == '"' && right == '"') || (left == '\'' && right == '\'')) {
            remember(EditKind::Deleting);
            erase_range(CodePos{ m_caret.line, m_caret.column - 1 }, CodePos{ m_caret.line, m_caret.column + 1 });
            m_caret = m_anchor = CodePos{ m_caret.line, m_caret.column - 1 };
            forget_preferred();
            changed();
            return;
        }
    }
    remember(EditKind::Deleting);
    erase_range(from, m_caret);
    m_caret = m_anchor = from;
    forget_preferred();
    changed();
}

void CodeBuffer::delete_forward(bool word) {
    if (has_selection()) {
        delete_selection();
        return;
    }
    const CodePos to = word ? word_right(m_caret) : step_right(m_caret);
    if (to == m_caret) return;
    remember(EditKind::Deleting);
    erase_range(m_caret, to);
    m_anchor = m_caret;
    forget_preferred();
    changed();
}

void CodeBuffer::indent(std::size_t tab_size, bool spaces) {
    if (tab_size == 0) tab_size = 4;
    const auto [a, b] = selection();
    if (a.line == b.line) {
        remember(EditKind::Other);
        if (has_selection()) {
            erase_range(a, b);
            m_caret = a;
        }
        std::string text = "\t";
        if (spaces) {
            const std::size_t col = code_display_column(m_lines[m_caret.line], m_caret.column, tab_size);
            text = std::string(tab_size - col % tab_size, ' ');
        }
        m_caret = m_anchor = insert_at(m_caret, text);
        forget_preferred();
        m_last_edit = EditKind::None;
        changed();
        return;
    }
    remember(EditKind::Other);
    const std::string unit = spaces ? std::string(tab_size, ' ') : std::string("\t");
    const std::size_t last = b.column == 0 ? b.line - 1 : b.line;
    for (std::size_t l = a.line; l <= last; ++l) {
        if (!m_lines[l].empty()) m_lines[l].insert(0, unit);
    }
    auto shift = [&](CodePos& p) {
        if (p.line >= a.line && p.line <= last && !m_lines[p.line].empty() && !(p.column == 0 && p.line == b.line && b.column == 0)) p.column += unit.size();
    };
    shift(m_caret);
    shift(m_anchor);
    m_last_edit = EditKind::None;
    changed();
}

void CodeBuffer::outdent(std::size_t tab_size) {
    if (tab_size == 0) tab_size = 4;
    const auto [a, b] = selection();
    const std::size_t last = (b.column == 0 && b.line > a.line) ? b.line - 1 : b.line;
    bool any = false;
    std::vector<std::size_t> removed(m_lines.size(), 0);
    for (std::size_t l = a.line; l <= last; ++l) {
        const std::string& s = m_lines[l];
        std::size_t n = 0;
        if (!s.empty() && s[0] == '\t') n = 1;
        else while (n < tab_size && n < s.size() && s[n] == ' ') ++n;
        removed[l] = n;
        any = any || n > 0;
    }
    if (!any) return;
    remember(EditKind::Other);
    for (std::size_t l = a.line; l <= last; ++l) m_lines[l].erase(0, removed[l]);
    auto shift = [&](CodePos& p) {
        if (p.line < removed.size()) p.column -= std::min(p.column, removed[p.line]);
    };
    shift(m_caret);
    shift(m_anchor);
    m_last_edit = EditKind::None;
    changed();
}

bool CodeBuffer::undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back(Snapshot{ m_lines, m_caret, m_anchor });
    Snapshot s = std::move(m_undo.back());
    m_undo.pop_back();
    m_lines = std::move(s.lines);
    m_caret = clamp(s.caret);
    m_anchor = clamp(s.anchor);
    m_last_edit = EditKind::None;
    forget_preferred();
    changed();
    return true;
}

bool CodeBuffer::redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(Snapshot{ m_lines, m_caret, m_anchor });
    Snapshot s = std::move(m_redo.back());
    m_redo.pop_back();
    m_lines = std::move(s.lines);
    m_caret = clamp(s.caret);
    m_anchor = clamp(s.anchor);
    m_last_edit = EditKind::None;
    forget_preferred();
    changed();
    return true;
}

} // namespace grove
