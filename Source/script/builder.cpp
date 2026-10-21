#include "grove/script/builder.hpp"
#include "grove/project.hpp"
#include "grove/api/grove_host.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace grove {

namespace {

std::uint64_t fnv(std::uint64_t h, std::string_view s) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    h ^= 0xFF;
    h *= 1099511628211ULL;
    return h;
}

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

bool file_exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

std::string quote_arg(const std::string& a) {
#if defined(_WIN32)
    std::string out = "\"";
    for (char c : a) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    return out + "\"";
#else
    std::string out = "'";
    for (char c : a) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

int run(const std::vector<std::string>& args, const std::string& log_path, const ScriptTools& tools, std::ostream* log) {
    std::string command;
    for (const std::string& a : args) {
        if (!command.empty()) command += ' ';
        command += quote_arg(a);
    }
    if (tools.verbose && log) *log << command << '\n';
    command += " > " + quote_arg(log_path) + " 2>&1";
#if defined(_WIN32)
    command = "\"" + command + "\"";
#endif
    const int status = std::system(command.c_str());
#if defined(_WIN32)
    return status;
#else
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

#if defined(__linux__)
bool static_runtime_available(const ScriptTools& tools) {
    static std::map<std::string, bool> known;
    const auto found = known.find(tools.cxx);
    if (found != known.end()) return found->second;
    bool ok = false;
    const std::string command = quote_arg(tools.cxx) + " -print-file-name=libstdc++.a 2>/dev/null";
    if (FILE* pipe = popen(command.c_str(), "r")) {
        std::string out;
        char buffer[512];
        while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
        pclose(pipe);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
        ok = !out.empty() && fs::path(out).is_absolute() && file_exists(out);
    }
    known[tools.cxx] = ok;
    return ok;
}
#endif

std::string capture_stdout(const std::string& command) {
#if defined(_WIN32)
    FILE* pipe = _popen(("\"" + command + " 2>NUL\"").c_str(), "r");
#else
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string c_compiler_beside(const std::string& cxx) {
    const fs::path p(cxx);
    std::string stem = p.stem().string();
    std::string c;
    if (stem.size() >= 7 && stem.compare(stem.size() - 7, 7, "clang++") == 0) c = stem.substr(0, stem.size() - 2);
    else if (stem.size() >= 3 && stem.compare(stem.size() - 3, 3, "g++") == 0) c = stem.substr(0, stem.size() - 3) + "gcc";
    else if (stem.size() >= 3 && stem.compare(stem.size() - 3, 3, "c++") == 0) c = stem.substr(0, stem.size() - 3) + "cc";
    if (c.empty()) return {};
    const fs::path candidate = p.parent_path() / (c + p.extension().string());
    return file_exists(candidate.string()) ? candidate.string() : std::string();
}

std::string append_log(const std::string& into, const std::string& from) {
    const std::optional<std::string> text = read_file(from);
    if (!text) return into;
    return into + *text;
}

std::string regex_escape(const std::string& s) {
    static const std::string special = "\\^$.|?*+()[]{}";
    std::string out;
    for (char c : s) {
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

std::string remap_paths(const std::string& text, const std::string& generated_name, const std::string& real_path) {
    if (generated_name.empty()) return text;
    const std::regex token("[^\\s:'\"]*" + regex_escape(generated_name));
    return std::regex_replace(text, token, real_path);
}

std::string first_lines(const std::string& text, std::size_t count) {
    std::istringstream in(text);
    std::string line;
    std::string out;
    std::size_t n = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (n == count) {
            out += "  ...\n";
            break;
        }
        out += "  " + line + "\n";
        ++n;
    }
    return out;
}

std::string raw_script_text(const ScriptBlock& s, std::size_t& first_line) {
    first_line = 1;
    if (!s.element) return s.code;
    for (const auto& c : s.element->children()) {
        if (!c->is_raw()) continue;
        first_line = c->text_span().begin.line;
        return c->text();
    }
    return s.code;
}

std::string language_key(ScriptLanguage l) { return to_string(l); }

bool is_walnut(ScriptLanguage l) { return l == ScriptLanguage::Walnut; }

bool needs_c_compile(ScriptLanguage l) { return l == ScriptLanguage::C; }

} // namespace

bool ScriptBuild::ok() const {
    for (const BuiltScript& s : scripts) {
        if (s.runnable && !s.ok) return false;
    }
    return true;
}

std::size_t ScriptBuild::runnable_count() const {
    std::size_t n = 0;
    for (const BuiltScript& s : scripts) n += s.runnable ? 1 : 0;
    return n;
}

std::string executable_path() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return fs::path(buffer).string();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.string();
#endif
}

std::string module_extension() {
#if defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

ScriptTools default_script_tools(const std::string& document_path) {
    ScriptTools t;
    const fs::path exe_dir = fs::path(executable_path()).parent_path();

    t.include_dir = env("GROVE_INCLUDE_DIR");
    if (t.include_dir.empty() && file_exists((exe_dir.parent_path() / "include" / "grove" / "dom.hpp").string())) t.include_dir = (exe_dir.parent_path() / "include").string();
#ifdef GROVE_SOURCE_INCLUDE_DIR
    if (t.include_dir.empty()) t.include_dir = GROVE_SOURCE_INCLUDE_DIR;
#endif

    t.walnut = env("GROVE_WALNUT");
#ifdef GROVE_WALNUT_EXECUTABLE
    if (t.walnut.empty() && file_exists(GROVE_WALNUT_EXECUTABLE)) t.walnut = GROVE_WALNUT_EXECUTABLE;
#endif
#if defined(_WIN32)
    const std::string walnut_name = "walnut.exe";
#else
    const std::string walnut_name = "walnut";
#endif
    if (t.walnut.empty() && file_exists((exe_dir / walnut_name).string())) t.walnut = (exe_dir / walnut_name).string();
    if (t.walnut.empty()) t.walnut = "walnut";

    t.cxx = env("CXX");
    t.cc = env("CC");
    t.compiler_from_walnut = t.cxx.empty();
#if defined(_WIN32)
    if (t.cxx.empty()) t.cxx = "g++";
    if (t.cc.empty()) t.cc = "gcc";
#else
    if (t.cxx.empty()) t.cxx = "c++";
    if (t.cc.empty()) t.cc = "cc";
#endif

    const fs::path doc(document_path);
    const fs::path dir = doc.parent_path().empty() ? fs::current_path() : doc.parent_path();
    const std::string cache_root = env("GROVE_CACHE_DIR");
    t.cache_dir = cache_root.empty() ? (dir / ".grove-cache" / doc.stem()).string() : (fs::path(cache_root) / doc.stem()).string();
    return t;
}

std::string walnut_compiler(const std::string& walnut) {
    if (walnut.empty()) return {};
    const std::string path = capture_stdout(quote_arg(walnut) + " init --print-compiler");
    if (path.empty() || path.find('\n') != std::string::npos || !file_exists(path)) return {};
    return path;
}

std::string describe(const ScriptTools& tools) {
    std::string cxx = tools.cxx;
    std::string cc = tools.cc;
    if (tools.compiler_from_walnut) {
        const std::string found = walnut_compiler(tools.walnut);
        if (!found.empty()) {
            cxx = found + " (the compiler walnut uses)";
            const std::string beside = env("CC").empty() ? c_compiler_beside(found) : std::string();
            if (!beside.empty()) cc = beside;
        }
    }
    return "walnut: " + tools.walnut + "\nc++: " + cxx + "\nc: " + cc + "\nheaders: " + tools.include_dir + "\ncache: " + tools.cache_dir + "\n";
}

ScriptBuild build_scripts(Project& project, const ScriptTools& requested, std::ostream* log) {
    ScriptTools tools = requested;
    bool compiler_resolved = !tools.compiler_from_walnut;
    auto resolve_compiler = [&] {
        if (compiler_resolved) return;
        compiler_resolved = true;
        const std::string cxx = walnut_compiler(tools.walnut);
        if (cxx.empty()) return;
        tools.cxx = cxx;
        if (env("CC").empty()) {
            const std::string cc = c_compiler_beside(cxx);
            if (!cc.empty()) tools.cc = cc;
        }
        if (tools.verbose && log) *log << "grove: using walnut's compiler " << cxx << '\n';
    };
    ScriptBuild out;
    std::error_code ec;
    fs::create_directories(tools.cache_dir, ec);

    const fs::path inc(tools.include_dir);
    const std::string glue = (inc / "grove" / "api" / "grove_module.cpp").string();
    const std::string walnut_modules = (inc / "grove" / "walnut").string();

    std::uint64_t base = 1469598103934665603ULL;
    base = fnv(base, std::to_string(GROVE_HOST_API_VERSION));
    for (const char* header : { "grove/api/grove_module.cpp", "grove/api/grove_host.h", "grove/api/grove_api_list.h", "grove/api/grove_api.h", "grove/dom.hpp" }) {
        base = fnv(base, read_file((inc / header).string()).value_or(std::string("missing ") + header));
    }
    base = fnv(base, tools.cxx);
    base = fnv(base, tools.cc);
    base = fnv(base, module_extension());
    base = fnv(base, tools.portable ? "portable" : "local");

    std::vector<std::string> header_dirs;
    for (const ScriptBlock& s : project.scripts) {
        if (!is_header(s.language)) continue;
        const std::string dir = s.inline_code ? project.base_dir : fs::path(s.source_path).parent_path().string();
        if (!dir.empty()) header_dirs.push_back(dir);
    }

    if (!file_exists(glue)) {
        if (!project.scripts.empty()) project.diagnostics.error(project.path, Span{}, "can't find Grove's script headers in '" + tools.include_dir + "'; set GROVE_INCLUDE_DIR to the folder that holds grove/dom.hpp");
        for (std::size_t i = 0; i < project.scripts.size(); ++i) {
            BuiltScript b;
            b.index = i;
            b.script = project.scripts[i];
            b.runnable = !is_header(b.script.language);
            out.scripts.push_back(std::move(b));
        }
        return out;
    }

    for (std::size_t i = 0; i < project.scripts.size(); ++i) {
        const ScriptBlock& s = project.scripts[i];
        BuiltScript built;
        built.index = i;
        built.script = s;
        built.runnable = !is_header(s.language);
        if (!built.runnable) {
            built.ok = true;
            out.scripts.push_back(std::move(built));
            continue;
        }

        if (is_walnut(s.language) && !s.entry.empty()) {
            project.diagnostics.warning(project.path, s.span, "entry=\"" + s.entry + "\" is only used by C and C++ scripts; a Walnut script runs its top-level code");
        }

        std::size_t first_line = 1;
        std::string source_text;
        std::string real_path;
        if (s.inline_code) {
            const std::string raw = raw_script_text(s, first_line);
            source_text = std::string(first_line > 0 ? first_line - 1 : 0, '\n') + raw;
            real_path = project.path;
        } else {
            source_text = read_file(s.source_path).value_or(std::string());
            real_path = s.source_path;
        }

        std::uint64_t h = fnv(base, language_key(s.language));
        h = fnv(h, s.entry);
        h = fnv(h, source_text);
        h = fnv(h, s.inline_code ? project.base_dir : fs::path(s.source_path).parent_path().string());
        if (is_walnut(s.language)) {
            h = fnv(h, tools.walnut);
            h = fnv(h, read_file((fs::path(walnut_modules) / "grove.walh").string()).value_or(std::string()));
        }
        for (const std::string& d : header_dirs) h = fnv(h, d);
        const std::string stem = "script" + std::to_string(i) + "-" + hex16(h);
        const fs::path module = fs::path(tools.cache_dir) / (stem + module_extension());
        const fs::path log_path = fs::path(tools.cache_dir) / (stem + ".log");
        built.module_path = module.string();

        if (!tools.rebuild && file_exists(module.string())) {
            built.ok = true;
            built.cached = true;
            out.scripts.push_back(std::move(built));
            continue;
        }
        resolve_compiler();

        for (const auto& entry : fs::directory_iterator(tools.cache_dir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("script" + std::to_string(i) + "-", 0) == 0 && name.find(hex16(h)) == std::string::npos) fs::remove(entry.path(), ec);
        }

        std::string source_path = s.source_path;
        std::string generated_name;
        if (s.inline_code) {
            const fs::path p = fs::path(tools.cache_dir) / (stem + file_extension(s.language));
            std::ofstream f(p, std::ios::binary);
            f << source_text;
            source_path = p.string();
            generated_name = p.filename().string();
        }
        const std::string script_dir = s.inline_code ? project.base_dir : fs::path(s.source_path).parent_path().string();

        std::vector<std::string> includes = { "-I" + tools.include_dir };
        if (!script_dir.empty()) includes.push_back("-I" + script_dir);
        for (const std::string& d : header_dirs) includes.push_back("-I" + d);

        std::vector<std::string> common = { "-std=c++17", "-O2", "-fPIC", "-fvisibility=hidden", "-DGROVE_SCRIPT_ID=0" };
#if defined(_WIN32)
        const std::vector<std::string> shared = { "-shared", "-static-libgcc", "-static-libstdc++" };
#else
        std::vector<std::string> shared = { "-shared" };
#if defined(__linux__)
        if (tools.portable && static_runtime_available(tools)) shared.insert(shared.end(), { "-static-libstdc++", "-static-libgcc" });
#endif
#endif
        if (!s.entry.empty() && !is_walnut(s.language)) common.push_back("-DGROVE_MODULE_ENTRY=" + s.entry);

        std::string combined_log;
        bool ok = true;
        const std::string step_log = (fs::path(tools.cache_dir) / (stem + ".step")).string();

        if (is_walnut(s.language)) {
            const std::string glue_obj = (fs::path(tools.cache_dir) / (stem + "-glue.o")).string();
            std::vector<std::string> cmd = { tools.cxx };
            cmd.insert(cmd.end(), common.begin(), common.end());
            cmd.push_back("-DGROVE_MODULE_WALNUT");
            cmd.insert(cmd.end(), includes.begin(), includes.end());
            cmd.insert(cmd.end(), { "-c", glue, "-o", glue_obj });
            ok = run(cmd, step_log, tools, log) == 0;
            combined_log = append_log(combined_log, step_log);
            if (ok) {
                std::vector<std::string> w = { tools.walnut, "build", source_path, glue_obj, "-o", module.string(), "--no-cache", "--cxx", tools.cxx, "-M", walnut_modules };
                if (!script_dir.empty()) w.insert(w.end(), { "-I", script_dir, "-M", script_dir });
                for (const std::string& f : { std::string("-fPIC"), std::string("-Dmain=grove_walnut_main") }) w.insert(w.end(), { "--cxx-flag", f });
                for (const std::string& f : shared) w.insert(w.end(), { "--cxx-flag", f });
                for (const std::string& f : includes) w.insert(w.end(), { "--cxx-flag", f });
                ok = run(w, step_log, tools, log) == 0;
                combined_log = append_log(combined_log, step_log);
            }
            fs::remove(glue_obj, ec);
        } else {
            std::vector<std::string> inputs = { glue };
            std::string object;
            if (needs_c_compile(s.language)) {
                object = (fs::path(tools.cache_dir) / (stem + "-c.o")).string();
                std::vector<std::string> cmd = { tools.cc, "-O2", "-fPIC", "-c", source_path, "-o", object };
                cmd.insert(cmd.end(), includes.begin(), includes.end());
                ok = run(cmd, step_log, tools, log) == 0;
                combined_log = append_log(combined_log, step_log);
                inputs.push_back(object);
            } else {
                inputs.push_back(source_path);
            }
            if (ok) {
                std::vector<std::string> cmd = { tools.cxx };
                cmd.insert(cmd.end(), common.begin(), common.end());
                cmd.insert(cmd.end(), shared.begin(), shared.end());
                cmd.insert(cmd.end(), includes.begin(), includes.end());
                cmd.insert(cmd.end(), inputs.begin(), inputs.end());
                cmd.insert(cmd.end(), { "-o", module.string() });
                ok = run(cmd, step_log, tools, log) == 0;
                combined_log = append_log(combined_log, step_log);
            }
            if (!object.empty()) fs::remove(object, ec);
        }
        fs::remove(step_log, ec);

        combined_log = remap_paths(combined_log, generated_name, real_path);
        {
            std::ofstream f(log_path, std::ios::binary);
            f << combined_log;
        }

        if (!ok || !file_exists(module.string())) {
            fs::remove(module, ec);
            project.diagnostics.error(project.path, s.span, "this " + std::string(to_string(s.language)) + " script failed to build (full log: " + log_path.string() + ")\n" + first_lines(combined_log, 12));
            built.ok = false;
        } else {
            built.ok = true;
        }
        out.scripts.push_back(std::move(built));
    }
    return out;
}

} // namespace grove
