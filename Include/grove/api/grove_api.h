#ifndef GROVE_API_H
#define GROVE_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) && defined(GROVE_API_SHARED)
#if defined(GROVE_API_BUILDING)
#define GROVE_API __declspec(dllexport)
#else
#define GROVE_API __declspec(dllimport)
#endif
#else
#define GROVE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grove_document grove_document;
typedef struct grove_element grove_element;

typedef void (*grove_entry_fn)(grove_document* document);

typedef enum grove_input_kind {
    GROVE_INPUT_NONE = 0,
    GROVE_INPUT_INTEGER = 1,
    GROVE_INPUT_FLOAT = 2,
    GROVE_INPUT_STRING = 3,
    GROVE_INPUT_BOOL = 4,
    GROVE_INPUT_RADIO = 5,
    GROVE_INPUT_SLIDER = 6,
    GROVE_INPUT_COLOR = 7
} grove_input_kind;

GROVE_API grove_element* grove_root(grove_document* document);
GROVE_API grove_element* grove_find_by_id(grove_document* document, const char* id);
GROVE_API grove_element* grove_first_by_tag(grove_document* document, const char* tag);

GROVE_API const char* grove_tag(const grove_element* element);
GROVE_API grove_element* grove_parent(grove_element* element);
GROVE_API size_t grove_child_count(const grove_element* element);
GROVE_API grove_element* grove_child(grove_element* element, size_t index);

GROVE_API int grove_has_attribute(const grove_element* element, const char* name);
GROVE_API const char* grove_get_attribute(const grove_element* element, const char* name);
GROVE_API void grove_set_attribute(grove_document* document, grove_element* element, const char* name, const char* value);
GROVE_API int grove_remove_attribute(grove_document* document, grove_element* element, const char* name);

GROVE_API size_t grove_get_text(const grove_element* element, char* buffer, size_t size);
GROVE_API void grove_set_text(grove_document* document, grove_element* element, const char* text);

GROVE_API unsigned long long grove_revision(const grove_document* document);

GROVE_API void grove_bind_document(grove_document* document);
GROVE_API grove_document* grove_current_document(void);
GROVE_API int grove_set_text_by_id(const char* id, const char* text);
GROVE_API const char* grove_get_attribute_by_id(const char* id, const char* name);
GROVE_API int grove_set_attribute_by_id(const char* id, const char* name, const char* value);

GROVE_API int grove_input_kind_of(const grove_element* element);
GROVE_API bool grove_input_integer(const grove_element* element, int64_t* out);
GROVE_API bool grove_input_float(const grove_element* element, double* out);
GROVE_API bool grove_input_bool(const grove_element* element, bool* out);
GROVE_API const char* grove_input_string(const grove_element* element);
GROVE_API bool grove_input_color(const grove_element* element, uint32_t* rgba);
GROVE_API bool grove_set_input_integer(grove_document* document, grove_element* element, int64_t value);
GROVE_API bool grove_set_input_float(grove_document* document, grove_element* element, double value);
GROVE_API bool grove_set_input_bool(grove_document* document, grove_element* element, bool value);
GROVE_API bool grove_set_input_string(grove_document* document, grove_element* element, const char* value);
GROVE_API bool grove_set_input_color(grove_document* document, grove_element* element, uint32_t rgba);
GROVE_API const char* grove_radio_choice(grove_document* document, const char* group);
GROVE_API bool grove_set_radio_choice(grove_document* document, const char* group, const char* value);

GROVE_API int grove_input_kind_by_id(const char* id);
GROVE_API int64_t grove_get_integer_by_id(const char* id);
GROVE_API double grove_get_float_by_id(const char* id);
GROVE_API bool grove_get_bool_by_id(const char* id);
GROVE_API const char* grove_get_string_by_id(const char* id);
GROVE_API uint32_t grove_get_color_by_id(const char* id);
GROVE_API bool grove_set_integer_by_id(const char* id, int64_t value);
GROVE_API bool grove_set_float_by_id(const char* id, double value);
GROVE_API bool grove_set_bool_by_id(const char* id, bool value);
GROVE_API bool grove_set_string_by_id(const char* id, const char* value);
GROVE_API bool grove_set_color_by_id(const char* id, uint32_t rgba);
GROVE_API const char* grove_get_choice(const char* group);
GROVE_API bool grove_set_choice(const char* group, const char* value);

#ifdef __cplusplus
}
#endif

#endif // GROVE_API_H
