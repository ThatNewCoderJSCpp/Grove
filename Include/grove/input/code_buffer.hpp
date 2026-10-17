#ifndef GROVE_INPUT_CODE_BUFFER_HPP
#define GROVE_INPUT_CODE_BUFFER_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace grove {

struct CodePos {
    std::size_t line = 0;
    std::size_t column = 0;

    bool operator==(const CodePos& o) const noexcept { return line == o.line && column == o.column; }
    bool operator!=(const CodePos& o) const noexcept { return !(*this == o); }
    bool operator<(const CodePos& o) const noexcept { return line < o.line || (line == o.line && column < o.column); }
};

enum class CodeLanguage {
    Plain,
    C,
    Cpp,
    Walnut,
    Grove,
    Bark
};

enum class CodeTokenKind {
    Plain,
    Keyword,
    Type,
    String,
    Number,
    Comment,
    Preprocessor,
    Function,
    Operator,
    Tag,
    Attribute,
    Property,
    Constant
};

struct CodeToken {
    std::size_t begin = 0;
    std::size_t end = 0;
    CodeTokenKind kind = CodeTokenKind::Plain;
};

struct CodeLineState {
    bool block_comment = false;
    bool markup_comment = false;

    bool operator==(const CodeLineState& o) const noexcept { return block_comment == o.block_comment && markup_comment == o.markup_comment; }
    bool operator!=(const CodeLineState& o) const noexcept { return !(*this == o); }
};

const char* to_string(CodeLanguage language) noexcept;
const char* to_string(CodeTokenKind kind) noexcept;
CodeLanguage code_language_from_name(std::string_view name) noexcept;
CodeLanguage code_language_for_path(std::string_view path) noexcept;

std::vector<CodeToken> highlight_code_line(CodeLanguage language, std::string_view line, CodeLineState& state);

std::size_t code_display_column(std::string_view line, std::size_t byte_column, std::size_t tab_size);
std::size_t code_byte_column(std::string_view line, std::size_t display_column, std::size_t tab_size);
std::string code_expand_tabs(std::string_view line, std::size_t tab_size);

class CodeBuffer {
public:
    CodeBuffer();
    explicit CodeBuffer(std::string_view text);

    void set_text(std::string_view text);
    std::string text() const;

    const std::vector<std::string>& lines() const noexcept { return m_lines; }
    std::size_t line_count() const noexcept { return m_lines.size(); }
    const std::string& line(std::size_t index) const { return m_lines[index]; }

    CodePos caret() const noexcept { return m_caret; }
    CodePos anchor() const noexcept { return m_anchor; }
    bool has_selection() const noexcept { return m_caret != m_anchor; }
    std::pair<CodePos, CodePos> selection() const noexcept;
    std::string selected_text() const;
    std::string text_between(CodePos a, CodePos b) const;

    CodePos clamp(CodePos p) const noexcept;
    void set_caret(CodePos p, bool extend = false);
    void select(CodePos anchor, CodePos caret);
    void select_all();
    void select_word_at(CodePos p);
    void select_line(std::size_t line);

    void move_left(bool extend, bool word = false);
    void move_right(bool extend, bool word = false);
    void move_up(bool extend, std::size_t lines = 1);
    void move_down(bool extend, std::size_t lines = 1);
    void move_home(bool extend);
    void move_end(bool extend);
    void move_start(bool extend);
    void move_finish(bool extend);

    void insert(std::string_view text);
    void new_line(std::size_t tab_size, bool spaces);
    void backspace(bool word = false);
    void delete_forward(bool word = false);
    void delete_selection();
    void indent(std::size_t tab_size, bool spaces);
    void outdent(std::size_t tab_size);

    bool undo();
    bool redo();
    bool can_undo() const noexcept { return !m_undo.empty(); }
    bool can_redo() const noexcept { return !m_redo.empty(); }

    std::size_t revision() const noexcept { return m_revision; }
    std::size_t preferred_column() const noexcept { return m_preferred; }
    void set_preferred_column(std::size_t column) noexcept { m_preferred = column; m_has_preferred = true; }

private:
    enum class EditKind {
        None,
        Typing,
        Deleting,
        Other
    };

    struct Snapshot {
        std::vector<std::string> lines;
        CodePos caret;
        CodePos anchor;
    };

    void remember(EditKind kind);
    void changed();
    void erase_range(CodePos a, CodePos b);
    CodePos insert_at(CodePos p, std::string_view text);
    CodePos step_left(CodePos p) const noexcept;
    CodePos step_right(CodePos p) const noexcept;
    CodePos word_left(CodePos p) const noexcept;
    CodePos word_right(CodePos p) const noexcept;
    void forget_preferred() noexcept { m_has_preferred = false; }

    std::vector<std::string> m_lines;
    CodePos m_caret;
    CodePos m_anchor;
    std::size_t m_preferred = 0;
    bool m_has_preferred = false;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    EditKind m_last_edit = EditKind::None;
    std::size_t m_revision = 0;
};

} // namespace grove

#endif // GROVE_INPUT_CODE_BUFFER_HPP
