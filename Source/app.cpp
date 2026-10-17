#include "grove/app.hpp"
#include "grove/version.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace grove {

namespace {

std::string hex16(std::uint64_t v) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[v & 15];
        v >>= 4;
    }
    return out;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

fs::path app_cache_root() {
#if defined(_WIN32)
    const std::string local = env("LOCALAPPDATA");
    if (!local.empty()) return fs::path(local) / "Grove" / "apps";
#else
    const std::string xdg = env("XDG_CACHE_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "grove" / "apps";
    const std::string home = env("HOME");
    if (!home.empty()) return fs::path(home) / ".cache" / "grove" / "apps";
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "grove-apps";
}

bool file_matches(const fs::path& path, const std::string& data) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) != data.size()) return false;
    std::ifstream in(path, std::ios::binary);
    std::string existing(data.size(), '\0');
    if (!in.read(existing.data(), static_cast<std::streamsize>(existing.size()))) return false;
    return existing == data;
}

bool write_file_atomically(const fs::path& path, const std::string& data) {
    const fs::path temp = path.string() + ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    std::error_code ec;
    fs::permissions(temp, fs::perms::owner_all, ec);
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return file_matches(path, data);
    }
    return true;
}

std::string format_errors(const Diagnostics& diagnostics) {
    std::string out;
    for (const Diagnostic& d : diagnostics.items()) {
        if (d.severity != Severity::Error) continue;
        out += d.file + ":" + std::to_string(d.span.begin.line) + ":" + std::to_string(d.span.begin.column) + ": error: " + d.message + "\n";
    }
    return out;
}

} // namespace

std::string default_app_output(const std::string& document_path) {
    fs::path p(document_path);
    p.replace_extension();
#if defined(_WIN32)
    p += ".exe";
#endif
    return p.string();
}

AppBuildResult build_app(const std::string& document_path, const AppBuildOptions& options, Diagnostics& diagnostics, std::ostream* log) {
    AppBuildResult result;
    std::vector<std::pair<std::string, std::string>> reads;
    set_file_read_observer([&reads](const std::string& path, const std::string& text) { reads.emplace_back(path, text); });
    Project project = load_project(document_path);
    set_file_read_observer(nullptr);

    if (!project.diagnostics.ok()) {
        diagnostics.append(project.diagnostics);
        return result;
    }

    ScriptTools tools = options.tools;
    tools.portable = true;
    const ScriptBuild scripts = build_scripts(project, tools, log);
    diagnostics.append(project.diagnostics);
    if (!scripts.ok() || !project.diagnostics.ok()) return result;

    const fs::path doc_abs = fs::absolute(document_path).lexically_normal();
    const fs::path base = doc_abs.parent_path();

    Bundle bundle;
    bundle.set_meta("grove-version", GROVE_VERSION_STRING);
    bundle.set_meta("document", doc_abs.filename().generic_string());
    bundle.set_meta("window", options.window_id);
    bundle.set_meta("module-extension", module_extension());

    std::vector<std::string> seen;
    for (const auto& [path, text] : reads) {
        const fs::path abs = fs::absolute(path).lexically_normal();
        fs::path rel = abs.lexically_relative(base);
        if (rel.empty()) rel = abs.filename();
        const std::string key = rel.generic_string();
        bool dup = false;
        for (const std::string& s : seen) dup = dup || s == key;
        if (dup) continue;
        seen.push_back(key);
        bundle.add("file", key, text);
        ++result.files;
    }

    for (const ScriptBlock& script : project.scripts) {
        if (script.inline_code || script.source_path.empty()) continue;
        fs::path rel = fs::absolute(script.source_path).lexically_normal().lexically_relative(base);
        if (rel.empty()) rel = fs::path(script.source_path).filename();
        bundle.add("source", rel.generic_string(), std::string());
    }

    for (const BuiltScript& b : scripts.scripts) {
        if (!b.runnable || !b.ok) continue;
        const std::optional<std::string> bytes = read_file(b.module_path);
        if (!bytes) {
            diagnostics.error(project.path, b.script.span, "can't read the built script '" + b.module_path + "'");
            return result;
        }
        bundle.add("module", std::to_string(b.index), *bytes);
        ++result.modules;
    }

    const std::string stub = options.stub.empty() ? executable_path() : options.stub;
    const std::string output = options.output.empty() ? default_app_output(document_path) : options.output;
    std::error_code ec;
    if (fs::exists(output, ec) && fs::equivalent(stub, output, ec)) {
        diagnostics.error(project.path, Span{}, "the output '" + output + "' is the grove program itself; pick another name with -o");
        return result;
    }

    std::string error;
    if (!write_bundled_executable(stub, output, bundle, error)) {
        diagnostics.error(project.path, Span{}, error);
        return result;
    }
    result.ok = true;
    result.output = output;
    result.bytes = fs::file_size(output, ec);
    return result;
}

bool load_app(const Bundle& bundle, LoadedApp& app, std::string& error) {
    const fs::path exe = executable_path();
    const fs::path root = exe.has_parent_path() ? exe.parent_path() : fs::current_path();

    std::map<std::string, std::string> files;
    for (const BundleEntry* e : bundle.all("source")) files[resolve_path(root.string(), e->name)] = std::string();
    for (const BundleEntry* e : bundle.all("file")) files[resolve_path(root.string(), e->name)] = e->data;
    set_virtual_files(std::move(files));

    const std::string document = bundle.meta("document");
    if (document.empty()) {
        error = "this app has no document";
        return false;
    }
    app.window_id = bundle.meta("window");
    app.project = load_project(resolve_path(root.string(), document));
    if (!app.project.diagnostics.ok()) {
        error = format_errors(app.project.diagnostics);
        return false;
    }

    const std::string ext = bundle.meta("module-extension", module_extension());
    const fs::path dir = app_cache_root() / hex16(bundle.hash());
    std::error_code ec;
    fs::create_directories(dir, ec);

    for (std::size_t i = 0; i < app.project.scripts.size(); ++i) {
        BuiltScript built;
        built.index = i;
        built.script = app.project.scripts[i];
        built.runnable = !is_header(built.script.language);
        if (!built.runnable) {
            built.ok = true;
            app.scripts.scripts.push_back(std::move(built));
            continue;
        }
        const BundleEntry* module = bundle.find("module", std::to_string(i));
        if (!module) {
            error = "this app is missing the code for script " + std::to_string(i + 1) + "; build it again";
            return false;
        }
        const fs::path path = dir / ("script" + std::to_string(i) + ext);
        if (!file_matches(path, module->data) && !write_file_atomically(path, module->data)) {
            error = "can't unpack the app's scripts into '" + dir.string() + "'";
            return false;
        }
        built.module_path = path.string();
        built.ok = true;
        built.cached = true;
        app.scripts.scripts.push_back(std::move(built));
    }
    return true;
}

} // namespace grove
