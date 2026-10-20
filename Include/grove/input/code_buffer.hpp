#ifndef GROVE_INPUT_CODE_BUFFER_HPP
#define GROVE_INPUT_CODE_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
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
    Bark,
    CMake,
    Html,
    Css,
    JavaScript,
    Json,
    Rust,
    Zig,
    Python,
    Lua,
    Java,
    CSharp,
    Assembly
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
    Constant,
    Namespace,
    Variable,
    Escape,
    ControlKeyword,
    BuiltinType,
    Declaration,
    SelfKeyword,
    ThisKeyword,
    Cast,
    BuiltinOperator,
    OverloadedOperator,
    EnumMember,
    TemplateScope,
    VariableTemplate,
    Unresolved
};

struct CodeToken {
    std::size_t begin = 0;
    std::size_t end = 0;
    CodeTokenKind kind = CodeTokenKind::Plain;
};

struct CodeLineState {
    bool block_comment = false;
    bool markup_comment = false;
    bool string_open = false;
    bool raw_string = false;
    std::string raw_delimiter;
    int bracket_level = -1;
    bool bracket_comment = false;
    int comment_depth = 0;
    int embedded = 0;

    bool operator==(const CodeLineState& o) const noexcept {
        return block_comment == o.block_comment && markup_comment == o.markup_comment && string_open == o.string_open && raw_string == o.raw_string &&
               raw_delimiter == o.raw_delimiter && bracket_level == o.bracket_level && bracket_comment == o.bracket_comment && comment_depth == o.comment_depth &&
               embedded == o.embedded;
    }
    bool operator!=(const CodeLineState& o) const noexcept { return !(*this == o); }
};

const char* to_string(CodeLanguage language) noexcept;
const char* to_string(CodeTokenKind kind) noexcept;
CodeLanguage code_language_from_name(std::string_view name) noexcept;
CodeLanguage code_language_for_path(std::string_view path) noexcept;
const std::vector<CodeLanguage>& code_languages() noexcept;
std::string code_line_comment(CodeLanguage language, std::string_view sample);
const std::vector<CodeTokenKind>& code_token_kinds() noexcept;
std::optional<CodeTokenKind> code_token_kind_from_name(std::string_view name) noexcept;
CodeTokenKind code_token_fallback(CodeTokenKind kind) noexcept;
const std::vector<CodeTokenKind>& code_token_kinds_for(CodeLanguage language) noexcept;

std::vector<CodeToken> highlight_code_line(CodeLanguage language, std::string_view line, CodeLineState& state);

struct CodeWord {
    std::string word;
    CodeTokenKind kind = CodeTokenKind::Keyword;
};

std::vector<CodeWord> code_language_words(CodeLanguage language);

struct CodeSpan {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
    CodeTokenKind kind = CodeTokenKind::Plain;
};

enum class CodeProblemKind {
    Error,
    Warning
};

struct CodeProblem {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
    CodeProblemKind kind = CodeProblemKind::Error;
};

struct CodeProblemMark {
    std::size_t begin = 0;
    std::size_t end = 0;
    CodeProblemKind kind = CodeProblemKind::Error;
};

std::uint64_t code_line_hash(std::string_view line) noexcept;
std::string code_spans_text(const std::vector<std::string>& lines, const std::vector<CodeSpan>& spans);
std::string code_problems_text(const std::vector<std::string>& lines, const std::vector<CodeProblem>& problems);
std::vector<CodeToken> overlay_code_tokens(const std::vector<CodeToken>& base, const std::vector<CodeToken>& over, std::size_t length);

class CodeOverlay {
public:
    void set_spans(std::string_view text);
    void set_problems(std::string_view text);
    void clear();
    bool empty() const noexcept { return m_spans.empty() && m_problems.empty(); }
    std::vector<CodeToken> spans(std::size_t line, std::string_view text) const;
    std::vector<CodeProblemMark> problems(std::size_t line, std::string_view text) const;
    bool has_error(std::size_t line, std::string_view text) const;

private:
    template <typename T>
    using Table = std::map<std::uint64_t, std::vector<std::pair<std::size_t, std::vector<T>>>>;

    template <typename T>
    static const std::vector<T>* nearest(const Table<T>& table, std::size_t line, std::string_view text);

    Table<CodeToken> m_spans;
    Table<CodeProblemMark> m_problems;
};

std::size_t code_display_column(std::string_view line, std::size_t byte_column, std::size_t tab_size);
std::size_t code_byte_column(std::string_view line, std::size_t display_column, std::size_t tab_size);
std::string code_expand_tabs(std::string_view line, std::size_t tab_size);
std::string code_block_closer(CodeLanguage language, std::string_view header);
std::string code_open_tag(std::string_view line);

struct CodeCursor {
    CodePos anchor;
    CodePos caret;

    bool has_selection() const noexcept { return anchor != caret; }
    CodePos start() const noexcept { return anchor < caret ? anchor : caret; }
    CodePos end() const noexcept { return anchor < caret ? caret : anchor; }
};

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
    void new_line(std::size_t tab_size, bool spaces, CodeLanguage language, bool close_blocks);
    void backspace(bool word = false);
    void backspace_to_tab_stop(std::size_t tab_size);
    void delete_forward(bool word = false);
    void delete_selection();
    void indent(std::size_t tab_size, bool spaces);
    void outdent(std::size_t tab_size);
    void toggle_comment(std::string_view marker);

    std::vector<CodeCursor> cursors() const;
    std::size_t cursor_count() const noexcept { return m_extra.size() + 1; }
    bool has_extra_cursors() const noexcept { return !m_extra.empty(); }
    void clear_extra_cursors();
    bool toggle_cursor(CodePos p);
    void add_cursor(CodePos anchor, CodePos caret);
    void column_select(CodePos from, std::size_t to_line);
    void box_select(std::size_t from_line, std::size_t from_column, std::size_t to_line, std::size_t to_column, std::size_t tab_size);
    bool any_selection() const noexcept;
    std::string selected_texts() const;
    void insert_each(const std::vector<std::string>& pieces);

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
        std::vector<CodeCursor> extra;
    };

    struct Tracked {
        CodeCursor cursor;
        std::size_t preferred = 0;
        bool has_preferred = false;
    };

    template <typename Fn>
    void for_each_cursor(Fn&& fn);
    void merge_cursors();
    std::vector<CodeCursor> cursors_extra() const;
    void track_erase(CodePos a, CodePos b);
    void track_insert(CodePos at, CodePos end);

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
    std::vector<Tracked> m_extra;
    std::vector<Tracked>* m_tracking = nullptr;
    CodePos m_batch_caret;
    CodePos m_batch_anchor;
    std::vector<CodeCursor> m_batch_extra;
    std::size_t m_current = 0;
    bool m_batch = false;
    bool m_batch_remembered = false;
    std::size_t m_preferred = 0;
    bool m_has_preferred = false;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    EditKind m_last_edit = EditKind::None;
    std::size_t m_revision = 0;
};

} // namespace grove

#endif // GROVE_INPUT_CODE_BUFFER_HPP
