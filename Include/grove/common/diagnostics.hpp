#ifndef GROVE_COMMON_DIAGNOSTICS_HPP
#define GROVE_COMMON_DIAGNOSTICS_HPP

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace grove {

struct Position {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Span {
    Position begin;
    Position end;
};

enum class Severity {
    Note,
    Warning,
    Error
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string file;
    Span span;
    std::string message;
};

class Diagnostics {
public:
    void note(const std::string& file, const Span& span, std::string message) { add(Severity::Note, file, span, std::move(message)); }
    void warning(const std::string& file, const Span& span, std::string message) { add(Severity::Warning, file, span, std::move(message)); }
    void error(const std::string& file, const Span& span, std::string message) { add(Severity::Error, file, span, std::move(message)); }

    void add(Severity severity, const std::string& file, const Span& span, std::string message) {
        m_items.push_back(Diagnostic{ severity, file, span, std::move(message) });
        if (severity == Severity::Error) ++m_errors;
        if (severity == Severity::Warning) ++m_warnings;
    }

    void append(const Diagnostics& other) {
        for (const Diagnostic& d : other.m_items) add(d.severity, d.file, d.span, d.message);
    }

    const std::vector<Diagnostic>& items() const noexcept { return m_items; }
    std::size_t error_count() const noexcept { return m_errors; }
    std::size_t warning_count() const noexcept { return m_warnings; }
    bool ok() const noexcept { return m_errors == 0; }
    bool empty() const noexcept { return m_items.empty(); }

    void print(std::ostream& os) const;

private:
    std::vector<Diagnostic> m_items;
    std::size_t m_errors = 0;
    std::size_t m_warnings = 0;
};

const char* to_string(Severity severity) noexcept;

} // namespace grove

#endif // GROVE_COMMON_DIAGNOSTICS_HPP
