#include "grove/native.hpp"

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
    const auto& dialog = document.host().file_dialog;
    if (!dialog) return {};
    return dialog(request);
}

} // namespace grove
