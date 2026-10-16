#include "grove/common/diagnostics.hpp"

namespace grove {

const char* to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Note: return "note";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
    }
    return "error";
}

void Diagnostics::print(std::ostream& os) const {
    for (const Diagnostic& d : m_items) {
        os << (d.file.empty() ? "<input>" : d.file) << ':' << d.span.begin.line << ':' << d.span.begin.column << ": " << to_string(d.severity) << ": " << d.message << '\n';
    }
}

} // namespace grove
