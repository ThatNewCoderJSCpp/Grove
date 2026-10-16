#ifndef GROVE_API_H
#define GROVE_API_H

#include <stddef.h>

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

#ifdef __cplusplus
}
#endif

#endif // GROVE_API_H
