#include "grove_host.h"
#include "../dom.hpp"

#include <exception>
#include <string>

#if defined(_WIN32)
#define GROVE_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

const grove_host_api* g_grove_host = nullptr;
std::string g_grove_module_error;

} // namespace

#define GROVE_MODULE_FORWARD(ret, name, params, args) extern "C" ret name params { return g_grove_host->name args; }
GROVE_API_FUNCTIONS(GROVE_MODULE_FORWARD)
#undef GROVE_MODULE_FORWARD

#ifdef GROVE_MODULE_WALNUT
int grove_walnut_main(int argc, char** argv);
#endif

#ifdef GROVE_MODULE_ENTRY
extern "C" void GROVE_MODULE_ENTRY(void);
#endif

GROVE_MODULE_EXPORT const char* grove_module_error(void) {
    return g_grove_module_error.c_str();
}

GROVE_MODULE_EXPORT int grove_module_init(const grove_host_api* host) {
    if (!host || host->version != GROVE_HOST_API_VERSION || host->size < sizeof(grove_host_api)) {
        g_grove_module_error = "this script was built for a different version of Grove; rebuild it";
        return 1;
    }
    g_grove_host = host;
    try {
        grove::detail::run_on_load(GROVE_SCRIPT_ID);
#ifdef GROVE_MODULE_ENTRY
        GROVE_MODULE_ENTRY();
#endif
#ifdef GROVE_MODULE_WALNUT
        char name[] = "grove-script";
        char* argv[] = { name, nullptr };
        if (grove_walnut_main(1, argv) != 0) {
            g_grove_module_error = "the Walnut script stopped with an error";
            return 1;
        }
#endif
    } catch (const std::exception& e) {
        g_grove_module_error = e.what();
        return 1;
    } catch (...) {
        g_grove_module_error = "the script threw an exception";
        return 1;
    }
    return 0;
}
