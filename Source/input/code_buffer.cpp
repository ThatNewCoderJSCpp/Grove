#include "grove/input/code_buffer.hpp"

#include <algorithm>
#include <numeric>

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
    m_extra.clear();
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
    if (!m_batch) m_extra.clear();
    m_caret = clamp(p);
    if (!extend) m_anchor = m_caret;
    forget_preferred();
    m_last_edit = EditKind::None;
}

void CodeBuffer::select(CodePos anchor, CodePos caret) {
    if (!m_batch) m_extra.clear();
    m_anchor = clamp(anchor);
    m_caret = clamp(caret);
    forget_preferred();
    m_last_edit = EditKind::None;
}

void CodeBuffer::select_all() {
    m_extra.clear();
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_left(extend, word); });
        return;
    }
    if (has_selection() && !extend) set_caret(selection().first);
    else set_caret(word ? word_left(m_caret) : step_left(m_caret), extend);
}

void CodeBuffer::move_right(bool extend, bool word) {
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_right(extend, word); });
        return;
    }
    if (has_selection() && !extend) set_caret(selection().second);
    else set_caret(word ? word_right(m_caret) : step_right(m_caret), extend);
}

void CodeBuffer::move_up(bool extend, std::size_t lines) {
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_up(extend, lines); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_down(extend, lines); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_home(extend); });
        return;
    }
    const std::size_t indent = leading_whitespace(m_lines[m_caret.line]);
    const std::size_t target = m_caret.column == indent ? 0 : indent;
    set_caret(CodePos{ m_caret.line, target }, extend);
}

void CodeBuffer::move_end(bool extend) {
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { move_end(extend); });
        return;
    }
    set_caret(CodePos{ m_caret.line, m_lines[m_caret.line].size() }, extend);
}

void CodeBuffer::move_start(bool extend) { set_caret(CodePos{}, extend); }

void CodeBuffer::move_finish(bool extend) { set_caret(CodePos{ m_lines.size() - 1, m_lines.back().size() }, extend); }

void CodeBuffer::remember(EditKind kind) {
    if (m_batch) {
        if (m_batch_remembered) {
            m_last_edit = kind;
            return;
        }
        m_batch_remembered = true;
    }
    const bool coalesce = kind != EditKind::Other && kind == m_last_edit && !m_undo.empty();
    if (!coalesce) {
        if (m_batch) m_undo.push_back(Snapshot{ m_lines, m_batch_caret, m_batch_anchor, m_batch_extra });
        else m_undo.push_back(Snapshot{ m_lines, m_caret, m_anchor, cursors_extra() });
        if (m_undo.size() > k_undo_limit) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_last_edit = kind;
}

void CodeBuffer::changed() { ++m_revision; }

void CodeBuffer::erase_range(CodePos a, CodePos b) {
    if (b < a) std::swap(a, b);
    if (a == b) return;
    track_erase(a, b);
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
        const CodePos end{ p.line, p.column + parts.front().size() };
        track_insert(p, end);
        return end;
    }
    std::vector<std::string> added(parts.begin() + 1, parts.end());
    const std::size_t end_column = added.back().size();
    added.back() += tail;
    m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(p.line) + 1, added.begin(), added.end());
    const CodePos end{ p.line + added.size(), end_column };
    track_insert(p, end);
    return end;
}

void CodeBuffer::insert(std::string_view text) {
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { insert(text); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { new_line(tab_size, spaces); });
        return;
    }
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
    erase_range(m_caret, CodePos{ m_caret.line, after });
    const CodePos start = m_caret;
    insert_at(start, text);
    m_caret = m_anchor = CodePos{ start.line + 1, caret_offset - 1 };
    forget_preferred();
    m_last_edit = EditKind::None;
    changed();
}

void CodeBuffer::delete_selection() {
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { delete_selection(); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { backspace(word); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { delete_forward(word); });
        return;
    }
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { indent(tab_size, spaces); });
        return;
    }
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
        if (!m_lines[l].empty()) insert_at(CodePos{ l, 0 }, unit);
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
    if (!m_extra.empty() && !m_batch) {
        for_each_cursor([&](std::size_t) { outdent(tab_size); });
        return;
    }
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
    for (std::size_t l = a.line; l <= last; ++l) erase_range(CodePos{ l, 0 }, CodePos{ l, removed[l] });
    auto shift = [&](CodePos& p) {
        if (p.line < removed.size()) p.column -= std::min(p.column, removed[p.line]);
    };
    shift(m_caret);
    shift(m_anchor);
    m_last_edit = EditKind::None;
    changed();
}

template <typename Fn>
void CodeBuffer::for_each_cursor(Fn&& fn) {
    std::vector<Tracked> all;
    all.push_back(Tracked{ CodeCursor{ m_anchor, m_caret }, m_preferred, m_has_preferred });
    for (const Tracked& t : m_extra) all.push_back(t);
    std::vector<std::size_t> order(all.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return all[x].cursor.start() < all[y].cursor.start(); });
    m_batch_caret = m_caret;
    m_batch_anchor = m_anchor;
    m_batch_extra = cursors_extra();
    m_batch = true;
    m_batch_remembered = false;
    m_tracking = &all;
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const std::size_t i = order[rank];
        m_current = i;
        m_anchor = all[i].cursor.anchor;
        m_caret = all[i].cursor.caret;
        m_preferred = all[i].preferred;
        m_has_preferred = all[i].has_preferred;
        fn(rank);
        all[i].cursor = CodeCursor{ m_anchor, m_caret };
        all[i].preferred = m_preferred;
        all[i].has_preferred = m_has_preferred;
    }
    m_tracking = nullptr;
    m_batch = false;
    m_anchor = all[0].cursor.anchor;
    m_caret = all[0].cursor.caret;
    m_preferred = all[0].preferred;
    m_has_preferred = all[0].has_preferred;
    m_extra.assign(all.begin() + 1, all.end());
    merge_cursors();
}

void CodeBuffer::track_erase(CodePos a, CodePos b) {
    if (!m_tracking) return;
    auto fix = [&](CodePos& p) {
        if (!(a < p)) return;
        if (p < b) {
            p = a;
            return;
        }
        if (p.line == b.line) {
            p.column = a.column + (p.column - b.column);
            p.line = a.line;
        } else {
            p.line -= b.line - a.line;
        }
    };
    for (std::size_t j = 0; j < m_tracking->size(); ++j) {
        if (j == m_current) continue;
        fix((*m_tracking)[j].cursor.anchor);
        fix((*m_tracking)[j].cursor.caret);
    }
}

void CodeBuffer::track_insert(CodePos at, CodePos end) {
    if (!m_tracking) return;
    auto fix = [&](CodePos& p) {
        if (p < at) return;
        if (p.line == at.line) {
            p.column = end.column + (p.column - at.column);
            p.line = end.line;
        } else {
            p.line += end.line - at.line;
        }
    };
    for (std::size_t j = 0; j < m_tracking->size(); ++j) {
        if (j == m_current) continue;
        fix((*m_tracking)[j].cursor.anchor);
        fix((*m_tracking)[j].cursor.caret);
    }
}

std::vector<CodeCursor> CodeBuffer::cursors_extra() const {
    std::vector<CodeCursor> out;
    for (const Tracked& t : m_extra) out.push_back(t.cursor);
    return out;
}

void CodeBuffer::merge_cursors() {
    if (m_extra.empty()) return;
    struct Item {
        Tracked t;
        bool primary;
    };
    std::vector<Item> items;
    items.push_back(Item{ Tracked{ CodeCursor{ m_anchor, m_caret }, m_preferred, m_has_preferred }, true });
    for (const Tracked& t : m_extra) items.push_back(Item{ t, false });
    std::stable_sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.t.cursor.start() < y.t.cursor.start(); });
    std::vector<Item> merged;
    for (Item& item : items) {
        item.t.cursor.anchor = clamp(item.t.cursor.anchor);
        item.t.cursor.caret = clamp(item.t.cursor.caret);
        if (!merged.empty()) {
            Item& last = merged.back();
            const CodeCursor& a = last.t.cursor;
            const CodeCursor& b = item.t.cursor;
            if (b.start() < a.end() || b.start() == a.start()) {
                const bool forward = !(a.caret < a.anchor);
                const CodePos lo = a.start();
                const CodePos hi = a.end() < b.end() ? b.end() : a.end();
                last.t.cursor = forward ? CodeCursor{ lo, hi } : CodeCursor{ hi, lo };
                last.primary = last.primary || item.primary;
                continue;
            }
        }
        merged.push_back(item);
    }
    m_extra.clear();
    bool have_primary = false;
    for (const Item& item : merged) {
        if (item.primary && !have_primary) {
            m_anchor = item.t.cursor.anchor;
            m_caret = item.t.cursor.caret;
            have_primary = true;
        } else {
            m_extra.push_back(item.t);
        }
    }
}

std::vector<CodeCursor> CodeBuffer::cursors() const {
    std::vector<CodeCursor> out;
    out.push_back(CodeCursor{ m_anchor, m_caret });
    for (const Tracked& t : m_extra) out.push_back(t.cursor);
    return out;
}

void CodeBuffer::clear_extra_cursors() {
    m_extra.clear();
}

bool CodeBuffer::toggle_cursor(CodePos p) {
    p = clamp(p);
    for (std::size_t i = 0; i < m_extra.size(); ++i) {
        if (m_extra[i].cursor.caret == p && !m_extra[i].cursor.has_selection()) {
            m_extra.erase(m_extra.begin() + static_cast<std::ptrdiff_t>(i));
            return false;
        }
    }
    if (m_caret == p && !has_selection() && !m_extra.empty()) {
        const Tracked last = m_extra.back();
        m_extra.pop_back();
        m_anchor = last.cursor.anchor;
        m_caret = last.cursor.caret;
        return false;
    }
    m_extra.push_back(Tracked{ CodeCursor{ p, p }, 0, false });
    merge_cursors();
    m_last_edit = EditKind::None;
    return true;
}

void CodeBuffer::add_cursor(CodePos anchor, CodePos caret) {
    m_extra.push_back(Tracked{ CodeCursor{ clamp(anchor), clamp(caret) }, 0, false });
    merge_cursors();
    m_last_edit = EditKind::None;
}

void CodeBuffer::column_select(CodePos from, std::size_t to_line) {
    from = clamp(from);
    if (to_line >= m_lines.size()) to_line = m_lines.size() - 1;
    const std::size_t want = code_points_before(m_lines[from.line], from.column);
    m_extra.clear();
    const std::size_t lo = std::min(from.line, to_line);
    const std::size_t hi = std::max(from.line, to_line);
    for (std::size_t l = lo; l <= hi; ++l) {
        const CodePos p{ l, byte_for_code_point(m_lines[l], want) };
        if (l == to_line) {
            m_caret = m_anchor = p;
            m_preferred = want;
            m_has_preferred = true;
        } else {
            m_extra.push_back(Tracked{ CodeCursor{ p, p }, want, true });
        }
    }
    m_last_edit = EditKind::None;
}

bool CodeBuffer::any_selection() const noexcept {
    if (has_selection()) return true;
    for (const Tracked& t : m_extra) {
        if (t.cursor.has_selection()) return true;
    }
    return false;
}

std::string CodeBuffer::selected_texts() const {
    std::vector<CodeCursor> all = cursors();
    std::sort(all.begin(), all.end(), [](const CodeCursor& x, const CodeCursor& y) { return x.start() < y.start(); });
    std::string out;
    bool first = true;
    for (const CodeCursor& c : all) {
        if (!c.has_selection()) continue;
        if (!first) out += '\n';
        out += text_between(c.start(), c.end());
        first = false;
    }
    return out;
}

void CodeBuffer::insert_each(const std::vector<std::string>& pieces) {
    if (pieces.empty()) return;
    if (m_extra.empty() || pieces.size() != cursor_count()) {
        std::string joined;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (i) joined += '\n';
            joined += pieces[i];
        }
        insert(joined);
        return;
    }
    for_each_cursor([&](std::size_t rank) { insert(pieces[rank]); });
}

bool CodeBuffer::undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back(Snapshot{ m_lines, m_caret, m_anchor, cursors_extra() });
    Snapshot s = std::move(m_undo.back());
    m_undo.pop_back();
    m_lines = std::move(s.lines);
    m_caret = clamp(s.caret);
    m_anchor = clamp(s.anchor);
    m_extra.clear();
    for (const CodeCursor& c : s.extra) m_extra.push_back(Tracked{ CodeCursor{ clamp(c.anchor), clamp(c.caret) }, 0, false });
    m_last_edit = EditKind::None;
    forget_preferred();
    changed();
    return true;
}

bool CodeBuffer::redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(Snapshot{ m_lines, m_caret, m_anchor, cursors_extra() });
    Snapshot s = std::move(m_redo.back());
    m_redo.pop_back();
    m_lines = std::move(s.lines);
    m_caret = clamp(s.caret);
    m_anchor = clamp(s.anchor);
    m_extra.clear();
    for (const CodeCursor& c : s.extra) m_extra.push_back(Tracked{ CodeCursor{ clamp(c.anchor), clamp(c.caret) }, 0, false });
    m_last_edit = EditKind::None;
    forget_preferred();
    changed();
    return true;
}

} // namespace grove
