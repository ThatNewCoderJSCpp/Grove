#ifndef GROVE_NATIVE_HPP
#define GROVE_NATIVE_HPP

#include "markup/node.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

void register_native(Document& document, std::string name, NativeFunction fn);
bool unregister_native(Document& document, std::string_view name);
bool has_native(const Document& document, std::string_view name);
std::vector<std::string> native_names(const Document& document);
std::optional<std::string> call_native(Document& document, std::string_view name, const std::vector<std::string>& args);

std::vector<std::string> run_file_dialog(Document& document, const FileDialogRequest& request);
bool set_clipboard_text(Document& document, const std::string& text);
std::string clipboard_text(Document& document);

} // namespace grove

#endif // GROVE_NATIVE_HPP
