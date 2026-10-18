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
typedef struct grove_event grove_event;

typedef void (*grove_entry_fn)(grove_document* document);
typedef void (*grove_listener_fn)(grove_event* event, void* user_data);
typedef void (*grove_timer_fn)(void* user_data);
typedef void (*grove_release_fn)(void* user_data);

typedef enum grove_input_kind {
    GROVE_INPUT_NONE = 0,
    GROVE_INPUT_INTEGER = 1,
    GROVE_INPUT_FLOAT = 2,
    GROVE_INPUT_STRING = 3,
    GROVE_INPUT_BOOL = 4,
    GROVE_INPUT_RADIO = 5,
    GROVE_INPUT_SLIDER = 6,
    GROVE_INPUT_COLOR = 7,
    GROVE_INPUT_CODE = 8,
    GROVE_INPUT_FILE = 9,
    GROVE_INPUT_SELECT = 10
} grove_input_kind;

typedef enum grove_modifier {
    GROVE_MOD_SHIFT = 1,
    GROVE_MOD_CONTROL = 2,
    GROVE_MOD_ALT = 4,
    GROVE_MOD_META = 8
} grove_modifier;

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
GROVE_API grove_element* grove_body(grove_document* document);
GROVE_API grove_element* grove_query(grove_document* document, grove_element* scope, const char* selector);
GROVE_API size_t grove_query_all(grove_document* document, grove_element* scope, const char* selector, grove_element** out, size_t capacity);
GROVE_API const char* grove_id(const grove_element* element);
GROVE_API const char* grove_text(const grove_element* element);
GROVE_API size_t grove_index_in_parent(const grove_element* element);
GROVE_API bool grove_is_attached(grove_document* document, const grove_element* element);
GROVE_API grove_element* grove_create_element(grove_document* document, const char* tag);
GROVE_API bool grove_append_child(grove_document* document, grove_element* parent, grove_element* child);
GROVE_API bool grove_insert_before(grove_document* document, grove_element* parent, grove_element* child, grove_element* reference);
GROVE_API bool grove_remove_element(grove_document* document, grove_element* element);
GROVE_API bool grove_has_class(const grove_element* element, const char* name);
GROVE_API void grove_add_class(grove_document* document, grove_element* element, const char* name);
GROVE_API void grove_remove_class(grove_document* document, grove_element* element, const char* name);
GROVE_API bool grove_toggle_class(grove_document* document, grove_element* element, const char* name);
GROVE_API const char* grove_get_style(const grove_element* element, const char* property);
GROVE_API void grove_set_style(grove_document* document, grove_element* element, const char* property, const char* value);
GROVE_API const char* grove_computed_style(grove_document* document, grove_element* element, const char* property);
GROVE_API bool grove_focus(grove_document* document, grove_element* element);
GROVE_API bool grove_has_state(const grove_element* element, const char* state);
GROVE_API uint64_t grove_add_event_listener(grove_document* document, grove_element* element, const char* type, grove_listener_fn fn, void* user_data, grove_release_fn release);
GROVE_API bool grove_remove_event_listener(grove_document* document, uint64_t id);
GROVE_API bool grove_dispatch_event(grove_document* document, grove_element* target, const char* type);
GROVE_API grove_event* grove_current_event(grove_document* document);
GROVE_API const char* grove_event_type(const grove_event* event);
GROVE_API grove_element* grove_event_target(grove_event* event);
GROVE_API grove_element* grove_event_current_target(grove_event* event);
GROVE_API double grove_event_x(const grove_event* event);
GROVE_API double grove_event_y(const grove_event* event);
GROVE_API double grove_event_delta_x(const grove_event* event);
GROVE_API double grove_event_delta_y(const grove_event* event);
GROVE_API int grove_event_button(const grove_event* event);
GROVE_API const char* grove_event_key(const grove_event* event);
GROVE_API const char* grove_event_text(const grove_event* event);
GROVE_API unsigned grove_event_modifiers(const grove_event* event);
GROVE_API void grove_event_stop_propagation(grove_event* event);
GROVE_API void grove_event_prevent_default(grove_event* event);
GROVE_API bool grove_event_default_prevented(const grove_event* event);
GROVE_API uint64_t grove_set_timeout(grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release);
GROVE_API uint64_t grove_set_interval(grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release);
GROVE_API bool grove_clear_timer(grove_document* document, uint64_t id);
GROVE_API double grove_time_ms(grove_document* document);
GROVE_API double grove_viewport_width(grove_document* document);
GROVE_API double grove_viewport_height(grove_document* document);
GROVE_API void grove_quit(grove_document* document);
GROVE_API void grove_log(const char* message);
GROVE_API grove_element* grove_main_window(grove_document* document);
GROVE_API grove_element* grove_window_of(grove_document* document, grove_element* element);
GROVE_API bool grove_window_open(grove_document* document, grove_element* window);
GROVE_API bool grove_window_close(grove_document* document, grove_element* window);
GROVE_API bool grove_window_is_open(grove_document* document, grove_element* window);
GROVE_API double grove_window_width(grove_document* document, grove_element* window);
GROVE_API double grove_window_height(grove_document* document, grove_element* window);
GROVE_API bool grove_window_resize(grove_document* document, grove_element* window, double width, double height);
GROVE_API bool grove_window_move(grove_document* document, grove_element* window, double x, double y);
GROVE_API bool grove_window_focus(grove_document* document, grove_element* window);
GROVE_API grove_element* grove_popup(grove_document* document, grove_element* owner, const char* kind, const char* title, const char* message, const char* input, const char* value, const char* options);
GROVE_API bool grove_popup_answer(grove_document* document, grove_element* popup, bool ok);
GROVE_API const char* grove_input_text(const grove_element* element);
GROVE_API const char* grove_call_native(grove_document* document, const char* name, const char* const* args, size_t count);
GROVE_API bool grove_has_native(grove_document* document, const char* name);
GROVE_API const char* grove_file_dialog(grove_document* document, const char* mode, const char* title, const char* filter, bool multiple);
GROVE_API bool grove_show_page(grove_document* document, grove_element* page);
GROVE_API bool grove_go_back(grove_document* document);
GROVE_API bool grove_open_modal(grove_document* document, grove_element* modal);
GROVE_API bool grove_close_modal(grove_document* document, grove_element* modal);
GROVE_API bool grove_navigate(grove_document* document, const char* target);
GROVE_API bool grove_is_current(const grove_element* element);

#ifdef __cplusplus
}
#endif

#endif // GROVE_API_H
