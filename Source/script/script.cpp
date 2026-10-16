#include "grove/script/script.hpp"
#include "grove/markup/parser.hpp"

#include <filesystem>

namespace grove {

namespace {

std::string lower_copy(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

} // namespace

const char* to_string(ScriptLanguage language) noexcept {
    switch (language) {
        case ScriptLanguage::Walnut: return "walnut";
        case ScriptLanguage::WalnutHeader: return "walnut-header";
        case ScriptLanguage::Cpp: return "c++";
        case ScriptLanguage::CppHeader: return "c++-header";
        case ScriptLanguage::C: return "c";
        case ScriptLanguage::CHeader: return "c-header";
    }
    return "walnut";
}

const char* file_extension(ScriptLanguage language) noexcept {
    switch (language) {
        case ScriptLanguage::Walnut: return ".wal";
        case ScriptLanguage::WalnutHeader: return ".walh";
        case ScriptLanguage::Cpp: return ".cpp";
        case ScriptLanguage::CppHeader: return ".hpp";
        case ScriptLanguage::C: return ".c";
        case ScriptLanguage::CHeader: return ".h";
    }
    return ".wal";
}

bool is_header(ScriptLanguage language) noexcept {
    return language == ScriptLanguage::WalnutHeader || language == ScriptLanguage::CppHeader || language == ScriptLanguage::CHeader;
}

std::optional<ScriptLanguage> language_from_name(std::string_view name) noexcept {
    const std::string n = lower_copy(name);
    if (n == "walnut" || n == "wal") return ScriptLanguage::Walnut;
    if (n == "walh" || n == "hwal" || n == "walnut-header") return ScriptLanguage::WalnutHeader;
    if (n == "c++" || n == "cpp" || n == "cxx" || n == "cc") return ScriptLanguage::Cpp;
    if (n == "hpp" || n == "hxx" || n == "c++-header" || n == "cpp-header") return ScriptLanguage::CppHeader;
    if (n == "c") return ScriptLanguage::C;
    if (n == "h" || n == "c-header") return ScriptLanguage::CHeader;
    return std::nullopt;
}

std::optional<ScriptLanguage> language_from_path(std::string_view path) noexcept {
    std::string ext = std::filesystem::path(std::string(path)).extension().string();
    if (ext.empty()) return std::nullopt;
    ext.erase(0, 1);
    if (ext == "C") return ScriptLanguage::Cpp;
    if (ext == "H") return ScriptLanguage::CppHeader;
    return language_from_name(ext);
}

std::vector<ScriptBlock> collect_scripts(const Document& document, const std::string& base_dir, Diagnostics& diagnostics) {
    std::vector<ScriptBlock> out;
    for (const Node* el : document.elements_by_tag("script")) {
        ScriptBlock block;
        block.span = el->span();
        block.element = el;
        block.entry = el->attribute_or("entry", "");

        const std::optional<std::string> src = el->attribute("src");
        const std::optional<std::string> lang = el->attribute("lang");
        const std::string code = dedent(el->inner_text());

        std::optional<ScriptLanguage> language;
        if (lang) {
            language = language_from_name(*lang);
            if (!language) {
                diagnostics.error(document.path(), el->span(), "unknown script language '" + *lang + "' (use walnut, c++ or c)");
                continue;
            }
        } else if (src) {
            language = language_from_path(*src);
            if (!language) {
                diagnostics.error(document.path(), el->span(), "can't tell the language of '" + *src + "'; add lang=\"walnut\", lang=\"c++\" or lang=\"c\"");
                continue;
            }
        } else {
            language = ScriptLanguage::Walnut;
        }
        block.language = *language;

        if (src) {
            if (!code.empty()) diagnostics.warning(document.path(), el->span(), "<script> has both src and inline code; the inline code is ignored");
            const std::filesystem::path p = std::filesystem::path(base_dir) / *src;
            block.source_path = p.lexically_normal().string();
            if (!std::filesystem::exists(p)) diagnostics.error(document.path(), el->span(), "script file '" + *src + "' does not exist");
        } else {
            if (code.empty()) {
                diagnostics.warning(document.path(), el->span(), "empty <script>");
                continue;
            }
            block.code = code;
            block.inline_code = true;
        }
        out.push_back(std::move(block));
    }
    return out;
}

} // namespace grove
