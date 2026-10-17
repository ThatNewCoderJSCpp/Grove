#include "grove/script/loader.hpp"
#include "grove/markup/node.hpp"
#include "grove/api/grove_host.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#else
#include <dlfcn.h>
#endif

namespace grove {

namespace {

struct Library {
    void* handle = nullptr;
    std::string error;
};

Library open_library(const std::string& path) {
    Library lib;
#if defined(_WIN32)
    HMODULE h = LoadLibraryW(std::filesystem::path(path).wstring().c_str());
    if (!h) lib.error = "Windows error " + std::to_string(GetLastError());
    lib.handle = reinterpret_cast<void*>(h);
#else
    lib.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib.handle) {
        const char* e = dlerror();
        lib.error = e ? e : "unknown error";
    }
#endif
    return lib;
}

void* find_symbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

} // namespace

std::size_t run_scripts(Document& document, const ScriptBuild& build, Diagnostics& diagnostics, const std::string& file) {
    grove_bind_document(reinterpret_cast<grove_document*>(&document));
    std::size_t ran = 0;
    for (const BuiltScript& s : build.scripts) {
        if (!s.runnable || !s.ok) continue;
        Library lib = open_library(s.module_path);
        if (!lib.handle) {
            diagnostics.error(file, s.script.span, "couldn't load the built script: " + lib.error);
            continue;
        }
        auto init = reinterpret_cast<grove_module_init_fn>(find_symbol(lib.handle, "grove_module_init"));
        auto error = reinterpret_cast<grove_module_error_fn>(find_symbol(lib.handle, "grove_module_error"));
        if (!init) {
            diagnostics.error(file, s.script.span, "the built script has no grove_module_init; rebuild it");
            continue;
        }
        if (init(grove_host_table()) != 0) {
            const std::string why = error ? error() : "unknown error";
            diagnostics.error(file, s.script.span, "the script failed while running: " + why);
            continue;
        }
        ++ran;
    }
    return ran;
}

} // namespace grove
