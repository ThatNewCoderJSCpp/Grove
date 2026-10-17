#ifndef GROVE_HOST_H
#define GROVE_HOST_H

#include "grove_api_list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GROVE_HOST_API_VERSION 1u

#define GROVE_HOST_FIELD(ret, name, params, args) ret (*name) params;

typedef struct grove_host_api {
    uint32_t version;
    uint32_t size;
    grove_document* document;
    GROVE_API_FUNCTIONS(GROVE_HOST_FIELD)
} grove_host_api;

#undef GROVE_HOST_FIELD

typedef int (*grove_module_init_fn)(const grove_host_api* host);
typedef const char* (*grove_module_error_fn)(void);

GROVE_API const grove_host_api* grove_host_table(void);

#ifdef __cplusplus
}
#endif

#endif // GROVE_HOST_H
