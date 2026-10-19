#ifndef GROVE_TERMINAL_HPP
#define GROVE_TERMINAL_HPP

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct TermColor {
    enum class Kind : std::uint8_t {
        Default,
        Indexed,
        Rgb
    };

    Kind kind = Kind::Default;
    std::uint8_t index = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    static TermColor indexed(int i) {
        TermColor c;
        c.kind = Kind::Indexed;
        c.index = static_cast<std::uint8_t>(i);
        return c;
    }
    static TermColor rgb(int red, int green, int blue) {
        TermColor c;
        c.kind = Kind::Rgb;
        c.r = static_cast<std::uint8_t>(red);
        c.g = static_cast<std::uint8_t>(green);
        c.b = static_cast<std::uint8_t>(blue);
        return c;
    }
    bool operator==(const TermColor& o) const noexcept { return kind == o.kind && index == o.index && r == o.r && g == o.g && b == o.b; }
    bool operator!=(const TermColor& o) const noexcept { return !(*this == o); }
};

enum TermAttr : std::uint16_t {
    TermBold = 1u << 0,
    TermDim = 1u << 1,
    TermItalic = 1u << 2,
    TermUnderline = 1u << 3,
    TermBlink = 1u << 4,
    TermInverse = 1u << 5,
    TermHidden = 1u << 6,
    TermStrike = 1u << 7,
    TermDoubleUnderline = 1u << 8,
    TermCurlyUnderline = 1u << 9,
    TermOverline = 1u << 10
};

struct TermCell {
    char32_t ch = U' ';
    std::uint8_t width = 1;
    std::uint16_t attrs = 0;
    TermColor fg;
    TermColor bg;
    std::u32string marks;

    bool same_style(const TermCell& o) const noexcept { return attrs == o.attrs && fg == o.fg && bg == o.bg; }
};

struct TermLine {
    std::vector<TermCell> cells;
    bool wrapped = false;
};

struct TermRgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    bool operator==(const TermRgb& o) const noexcept { return r == o.r && g == o.g && b == o.b; }
};

enum class TermKey {
    None,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    Delete,
    Backspace,
    Tab,
    Enter,
    Escape,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    KeypadEnter
};

enum TermModifier : unsigned {
    TermShift = 1u,
    TermAlt = 2u,
    TermCtrl = 4u,
    TermMeta = 8u
};

enum class TermMouseMode {
    None,
    Press,
    PressRelease,
    ButtonMotion,
    AnyMotion
};

enum class TermMouseEncoding {
    Default,
    Utf8,
    Sgr,
    Urxvt
};

enum class TermCursorShape {
    Block,
    Underline,
    Bar
};

struct TermMouse {
    int button = 0;
    bool press = true;
    bool motion = false;
    int column = 0;
    int row = 0;
    unsigned modifiers = 0;
};

struct TermPoint {
    long long line = 0;
    int column = 0;
    bool operator==(const TermPoint& o) const noexcept { return line == o.line && column == o.column; }
    bool operator<(const TermPoint& o) const noexcept { return line < o.line || (line == o.line && column < o.column); }
};

class Document;
class Node;

int terminal_char_width(char32_t c) noexcept;

class Terminal {
public:
    explicit Terminal(int columns = 80, int rows = 24, std::size_t scrollback = 10000);

    void feed(std::string_view bytes);
    void resize(int columns, int rows);
    void reset();

    int columns() const noexcept { return m_cols; }
    int rows() const noexcept { return m_rows; }
    const TermLine& line(int row) const;
    std::size_t scrollback_size() const noexcept { return m_history.size(); }
    const TermLine& history_line(std::size_t index) const { return m_history[index]; }
    void set_scrollback_limit(std::size_t lines);
    void clear_scrollback();

    int view_offset() const noexcept { return m_view; }
    void scroll_view(int lines);
    void reset_view() noexcept { m_view = 0; }
    const TermLine& view_line(int row) const;
    long long view_line_number(int row) const noexcept { return static_cast<long long>(m_history.size()) - m_view + row; }

    int cursor_column() const noexcept { return m_cur.x; }
    int cursor_row() const noexcept { return m_cur.y; }
    bool cursor_visible() const noexcept { return m_cursor_visible; }
    TermCursorShape cursor_shape() const noexcept { return m_cursor_shape; }
    bool cursor_blinks() const noexcept { return m_cursor_blink; }

    bool alternate_screen() const noexcept { return m_alt_active; }
    bool application_cursor_keys() const noexcept { return m_app_cursor; }
    bool application_keypad() const noexcept { return m_app_keypad; }
    bool bracketed_paste() const noexcept { return m_bracketed_paste; }
    bool reverse_video() const noexcept { return m_reverse_video; }
    bool focus_reporting() const noexcept { return m_focus_events; }
    bool alternate_scroll() const noexcept { return m_alternate_scroll; }
    bool insert_mode() const noexcept { return m_insert; }
    bool origin_mode() const noexcept { return m_cur.origin; }
    bool autowrap() const noexcept { return m_cur.autowrap; }
    int scroll_top() const noexcept { return m_top; }
    int scroll_bottom() const noexcept { return m_bottom; }
    TermMouseMode mouse_mode() const noexcept { return m_mouse_mode; }
    TermMouseEncoding mouse_encoding() const noexcept { return m_mouse_encoding; }
    const TermCell& pen() const noexcept { return m_cur.pen; }

    const std::string& title() const noexcept { return m_title; }
    const std::string& working_directory() const noexcept { return m_cwd; }

    using InputHandler = std::function<void(const std::string&)>;
    void set_input_handler(InputHandler handler) { m_input = std::move(handler); }
    bool send(const std::string& bytes);

    std::string take_reply();
    bool take_changed() noexcept;
    bool take_bell() noexcept;
    bool take_title_changed() noexcept;

    std::string encode_key(TermKey key, unsigned modifiers) const;
    std::string encode_char(char32_t c, unsigned modifiers) const;
    std::string encode_text(std::string_view text) const;
    std::string encode_paste(std::string_view text) const;
    std::string encode_mouse(const TermMouse& mouse) const;
    std::string encode_focus(bool focused) const;
    std::string encode_wheel(bool up, int column, int row, unsigned modifiers) const;

    void set_palette(int index, TermRgb rgb);
    TermRgb palette(int index) const;
    void reset_palette();
    void set_default_colors(TermRgb foreground, TermRgb background, TermRgb cursor);
    TermRgb default_foreground() const noexcept { return m_default_fg; }
    TermRgb default_background() const noexcept { return m_default_bg; }
    TermRgb cursor_color() const noexcept { return m_cursor_color; }
    TermRgb resolve(const TermColor& color, bool foreground) const;

    void select(TermPoint from, TermPoint to, bool block = false);
    void select_word(TermPoint at);
    void select_line(TermPoint at);
    void select_all();
    void clear_selection() noexcept;
    bool has_selection() const noexcept { return m_sel_active && !(m_sel_from == m_sel_to); }
    bool is_selected(long long line, int column) const noexcept;
    std::string selected_text() const;
    TermPoint selection_from() const noexcept { return m_sel_from; }
    TermPoint selection_to() const noexcept { return m_sel_to; }

    std::string row_text(int row) const;
    std::string screen_text() const;
    long long total_lines() const noexcept { return static_cast<long long>(m_history.size()) + m_rows; }
    const TermLine& absolute_line(long long line) const;

private:
    struct Cursor {
        int x = 0;
        int y = 0;
        TermCell pen;
        bool pending_wrap = false;
        bool origin = false;
        bool autowrap = true;
        std::array<char, 4> charsets{ { 'B', 'B', 'B', 'B' } };
        int gl = 0;
        int single_shift = -1;
    };

    enum class State {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        CsiParam,
        CsiIntermediate,
        CsiIgnore,
        Osc,
        Dcs,
        String
    };

    std::vector<TermLine>& screen() noexcept { return m_alt_active ? m_alt : m_main; }
    const std::vector<TermLine>& screen() const noexcept { return m_alt_active ? m_alt : m_main; }
    TermCell blank() const;
    TermLine blank_line() const;
    void touch() noexcept { m_changed = true; }

    void byte(unsigned char c);
    void control(unsigned char c);
    void print(char32_t c);
    void escape_dispatch(unsigned char final);
    void csi_dispatch(unsigned char final);
    void osc_dispatch();
    void dcs_dispatch();
    void sgr();
    void set_mode(int mode, bool on, bool priv);
    int mode_state(int mode, bool priv) const;

    int param(std::size_t i, int fallback) const;
    void clear_params();

    void line_feed();
    void reverse_index();
    void carriage_return();
    void scroll_up(int top, int bottom, int count, bool keep = true);
    void scroll_down(int top, int bottom, int count);
    void move_to(int x, int y);
    void clamp_cursor();
    void erase_cells(int row, int from, int to);
    void erase_display(int mode);
    void erase_line(int mode);
    void insert_chars(int count);
    void delete_chars(int count);
    void insert_lines(int count);
    void delete_lines(int count);
    void fix_wide_at(int row, int column);
    void tab_forward(int count);
    void tab_backward(int count);
    void save_cursor();
    void restore_cursor();
    void enter_alternate(bool clear, bool save);
    void leave_alternate(bool restore);
    void soft_reset();
    void push_history(TermLine line);
    void reply(const std::string& text) { m_reply += text; }
    char32_t translate(char32_t c) const;
    std::string color_report(int which) const;
    void shift_selection(long long lines);

    int m_cols;
    int m_rows;
    std::size_t m_limit;
    std::vector<TermLine> m_main;
    std::vector<TermLine> m_alt;
    std::deque<TermLine> m_history;
    bool m_alt_active = false;
    Cursor m_cur;
    Cursor m_saved_main;
    Cursor m_saved_alt;
    int m_top = 0;
    int m_bottom = 0;
    std::vector<bool> m_tabs;
    bool m_insert = false;
    bool m_newline_mode = false;
    bool m_app_cursor = false;
    bool m_app_keypad = false;
    bool m_cursor_visible = true;
    bool m_cursor_blink = false;
    TermCursorShape m_cursor_shape = TermCursorShape::Block;
    bool m_bracketed_paste = false;
    bool m_reverse_video = false;
    bool m_focus_events = false;
    bool m_alternate_scroll = true;
    bool m_sync = false;
    TermMouseMode m_mouse_mode = TermMouseMode::None;
    TermMouseEncoding m_mouse_encoding = TermMouseEncoding::Default;
    int m_view = 0;

    State m_state = State::Ground;
    std::vector<int> m_params;
    std::vector<std::vector<int>> m_subparams;
    bool m_param_started = false;
    bool m_sub = false;
    std::string m_intermediates;
    char m_prefix = 0;
    std::string m_string;
    bool m_string_escape = false;
    State m_string_kind = State::Ground;
    std::uint32_t m_utf8 = 0;
    int m_utf8_left = 0;
    char32_t m_last_char = 0;

    std::string m_reply;
    std::string m_title;
    std::string m_cwd;
    bool m_changed = true;
    bool m_bell = false;
    bool m_title_changed = false;

    std::array<TermRgb, 256> m_palette{};
    TermRgb m_default_fg{ 204, 204, 204 };
    TermRgb m_default_bg{ 30, 30, 30 };
    TermRgb m_cursor_color{ 255, 255, 255 };
    TermRgb m_base_fg{ 204, 204, 204 };
    TermRgb m_base_bg{ 30, 30, 30 };
    TermRgb m_base_cursor{ 255, 255, 255 };

    InputHandler m_input;
    bool m_sel_active = false;
    bool m_sel_block = false;
    TermPoint m_sel_from;
    TermPoint m_sel_to;
};

std::shared_ptr<Terminal> terminal_of(Document& document, const Node& element);
std::shared_ptr<Terminal> find_terminal(const Document& document, const Node& element);
void forget_terminal(Document& document, const Node& element);

} // namespace grove

#endif // GROVE_TERMINAL_HPP
