#ifndef GROVE_COMMON_CURSOR_HPP
#define GROVE_COMMON_CURSOR_HPP

#include "diagnostics.hpp"

#include <string_view>

namespace grove {

class Cursor {
public:
    explicit Cursor(std::string_view text, Position origin = Position{}) noexcept : m_text(text), m_pos(origin), m_base(origin.offset) {}

    bool at_end() const noexcept { return index() >= m_text.size(); }
    char peek(std::size_t ahead = 0) const noexcept {
        const std::size_t i = index() + ahead;
        return i < m_text.size() ? m_text[i] : '\0';
    }

    bool starts_with(std::string_view s) const noexcept { return m_text.substr(index(), s.size()) == s; }

    bool starts_with_icase(std::string_view s) const noexcept {
        if (m_text.size() - index() < s.size()) return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (lower(m_text[index() + i]) != lower(s[i])) return false;
        }
        return true;
    }

    char advance() noexcept {
        if (at_end()) return '\0';
        const char c = m_text[index()];
        ++m_pos.offset;
        if (c == '\n') {
            ++m_pos.line;
            m_pos.column = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++m_pos.column;
        }
        return c;
    }

    void advance(std::size_t n) noexcept {
        while (n-- > 0 && !at_end()) advance();
    }

    bool match(std::string_view s) noexcept {
        if (!starts_with(s)) return false;
        advance(s.size());
        return true;
    }

    void skip_whitespace() noexcept {
        while (!at_end() && is_space(peek())) advance();
    }

    const Position& position() const noexcept { return m_pos; }
    std::string_view text() const noexcept { return m_text; }
    std::string_view slice(const Position& from) const noexcept { return m_text.substr(from.offset - m_base, m_pos.offset - from.offset); }
    Span span_from(const Position& from) const noexcept { return Span{ from, m_pos }; }

    static bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
    static bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
    static bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    static char lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

private:
    std::size_t index() const noexcept { return m_pos.offset - m_base; }

    std::string_view m_text;
    Position m_pos;
    std::size_t m_base = 0;
};

} // namespace grove

#endif // GROVE_COMMON_CURSOR_HPP
