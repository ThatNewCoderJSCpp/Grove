#include "grove/native.hpp"

#include <cctype>
#include <exception>

namespace grove {

void register_native(Document& document, std::string name, NativeFunction fn) {
    if (!fn) {
        unregister_native(document, name);
        return;
    }
    document.host().natives[std::move(name)] = std::move(fn);
}

bool unregister_native(Document& document, std::string_view name) {
    auto& natives = document.host().natives;
    const auto it = natives.find(name);
    if (it == natives.end()) return false;
    natives.erase(it);
    return true;
}

bool has_native(const Document& document, std::string_view name) {
    const auto& natives = document.host().natives;
    return natives.find(name) != natives.end();
}

std::vector<std::string> native_names(const Document& document) {
    std::vector<std::string> out;
    for (const auto& entry : document.host().natives) out.push_back(entry.first);
    return out;
}

std::optional<std::string> call_native(Document& document, std::string_view name, const std::vector<std::string>& args) {
    const auto& natives = document.host().natives;
    const auto it = natives.find(name);
    if (it == natives.end()) {
        document.events().report_error("no native function named '" + std::string(name) + "'");
        return std::nullopt;
    }
    const NativeFunction fn = it->second;
    try {
        return fn(args);
    } catch (const std::exception& e) {
        document.events().report_error("native function '" + std::string(name) + "' failed: " + e.what());
    } catch (...) {
        document.events().report_error("native function '" + std::string(name) + "' failed");
    }
    return std::string();
}

std::vector<std::string> run_file_dialog(Document& document, const FileDialogRequest& request) {
    std::vector<std::string> rejected;
    return run_file_dialog(document, request, rejected);
}

std::vector<std::string> run_file_dialog(Document& document, const FileDialogRequest& request, std::vector<std::string>& rejected) {
    rejected.clear();
    const auto& dialog = document.host().file_dialog;
    if (!dialog) return {};
    std::vector<std::string> paths = dialog(request);
    rejected = remove_disallowed_files(request, paths);
    return paths;
}

std::vector<std::string> file_patterns(std::string_view filter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < filter.size()) {
        std::size_t end = filter.find(';', start);
        if (end == std::string_view::npos) end = filter.size();
        std::string_view entry = filter.substr(start, end - start);
        start = end + 1;
        const std::size_t colon = entry.find(':');
        if (colon != std::string_view::npos) entry = entry.substr(colon + 1);
        std::string current;
        for (std::size_t i = 0; i <= entry.size(); ++i) {
            const char c = i < entry.size() ? entry[i] : ' ';
            if (c == ' ' || c == ',' || c == '\t') {
                if (!current.empty()) out.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
    }
    return out;
}

bool file_pattern_matches(std::string_view pattern, std::string_view name) {
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string_view::npos;
    std::size_t resume = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(name[n]))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool file_allowed(const FileDialogRequest& request, const std::string& path) {
    std::string name = path;
    while (name.size() > 1 && (name.back() == '/' || name.back() == '\\')) name.pop_back();
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    for (const std::string& p : file_patterns(request.reject)) {
        if (file_pattern_matches(p, name)) return false;
    }
    if (!request.enforce || request.mode == "folder") return true;
    const std::vector<std::string> allowed = file_patterns(request.filter);
    if (allowed.empty()) return true;
    for (const std::string& p : allowed) {
        if (p == "*" || p == "*.*" || file_pattern_matches(p, name)) return true;
    }
    return false;
}

std::vector<std::string> remove_disallowed_files(const FileDialogRequest& request, std::vector<std::string>& paths) {
    std::vector<std::string> rejected;
    if (request.reject.empty() && !request.enforce) return rejected;
    std::vector<std::string> kept;
    for (std::string& p : paths) {
        if (file_allowed(request, p)) kept.push_back(std::move(p));
        else rejected.push_back(std::move(p));
    }
    paths.swap(kept);
    return rejected;
}

bool set_clipboard_text(Document& document, const std::string& text) {
    const auto& fn = document.host().set_clipboard;
    return fn ? fn(text) : false;
}

std::string clipboard_text(Document& document) {
    const auto& fn = document.host().clipboard;
    return fn ? fn() : std::string();
}

} // namespace grove
