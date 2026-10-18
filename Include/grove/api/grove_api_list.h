#ifndef GROVE_API_LIST_H
#define GROVE_API_LIST_H

#include "grove_api.h"

#define GROVE_API_FUNCTIONS(X) \
    X(grove_element*, grove_root, (grove_document* document), (document)) \
    X(grove_element*, grove_find_by_id, (grove_document* document, const char* id), (document, id)) \
    X(grove_element*, grove_first_by_tag, (grove_document* document, const char* tag), (document, tag)) \
    X(const char*, grove_tag, (const grove_element* element), (element)) \
    X(grove_element*, grove_parent, (grove_element* element), (element)) \
    X(size_t, grove_child_count, (const grove_element* element), (element)) \
    X(grove_element*, grove_child, (grove_element* element, size_t index), (element, index)) \
    X(int, grove_has_attribute, (const grove_element* element, const char* name), (element, name)) \
    X(const char*, grove_get_attribute, (const grove_element* element, const char* name), (element, name)) \
    X(void, grove_set_attribute, (grove_document* document, grove_element* element, const char* name, const char* value), (document, element, name, value)) \
    X(int, grove_remove_attribute, (grove_document* document, grove_element* element, const char* name), (document, element, name)) \
    X(size_t, grove_get_text, (const grove_element* element, char* buffer, size_t size), (element, buffer, size)) \
    X(void, grove_set_text, (grove_document* document, grove_element* element, const char* text), (document, element, text)) \
    X(unsigned long long, grove_revision, (const grove_document* document), (document)) \
    X(void, grove_bind_document, (grove_document* document), (document)) \
    X(grove_document*, grove_current_document, (void), ()) \
    X(int, grove_set_text_by_id, (const char* id, const char* text), (id, text)) \
    X(const char*, grove_get_attribute_by_id, (const char* id, const char* name), (id, name)) \
    X(int, grove_set_attribute_by_id, (const char* id, const char* name, const char* value), (id, name, value)) \
    X(int, grove_input_kind_of, (const grove_element* element), (element)) \
    X(bool, grove_input_integer, (const grove_element* element, int64_t* out), (element, out)) \
    X(bool, grove_input_float, (const grove_element* element, double* out), (element, out)) \
    X(bool, grove_input_bool, (const grove_element* element, bool* out), (element, out)) \
    X(const char*, grove_input_string, (const grove_element* element), (element)) \
    X(bool, grove_input_color, (const grove_element* element, uint32_t* rgba), (element, rgba)) \
    X(bool, grove_set_input_integer, (grove_document* document, grove_element* element, int64_t value), (document, element, value)) \
    X(bool, grove_set_input_float, (grove_document* document, grove_element* element, double value), (document, element, value)) \
    X(bool, grove_set_input_bool, (grove_document* document, grove_element* element, bool value), (document, element, value)) \
    X(bool, grove_set_input_string, (grove_document* document, grove_element* element, const char* value), (document, element, value)) \
    X(bool, grove_set_input_color, (grove_document* document, grove_element* element, uint32_t rgba), (document, element, rgba)) \
    X(const char*, grove_radio_choice, (grove_document* document, const char* group), (document, group)) \
    X(bool, grove_set_radio_choice, (grove_document* document, const char* group, const char* value), (document, group, value)) \
    X(int, grove_input_kind_by_id, (const char* id), (id)) \
    X(int64_t, grove_get_integer_by_id, (const char* id), (id)) \
    X(double, grove_get_float_by_id, (const char* id), (id)) \
    X(bool, grove_get_bool_by_id, (const char* id), (id)) \
    X(const char*, grove_get_string_by_id, (const char* id), (id)) \
    X(uint32_t, grove_get_color_by_id, (const char* id), (id)) \
    X(bool, grove_set_integer_by_id, (const char* id, int64_t value), (id, value)) \
    X(bool, grove_set_float_by_id, (const char* id, double value), (id, value)) \
    X(bool, grove_set_bool_by_id, (const char* id, bool value), (id, value)) \
    X(bool, grove_set_string_by_id, (const char* id, const char* value), (id, value)) \
    X(bool, grove_set_color_by_id, (const char* id, uint32_t rgba), (id, rgba)) \
    X(const char*, grove_get_choice, (const char* group), (group)) \
    X(bool, grove_set_choice, (const char* group, const char* value), (group, value)) \
    X(grove_element*, grove_body, (grove_document* document), (document)) \
    X(grove_element*, grove_query, (grove_document* document, grove_element* scope, const char* selector), (document, scope, selector)) \
    X(size_t, grove_query_all, (grove_document* document, grove_element* scope, const char* selector, grove_element** out, size_t capacity), (document, scope, selector, out, capacity)) \
    X(const char*, grove_id, (const grove_element* element), (element)) \
    X(const char*, grove_text, (const grove_element* element), (element)) \
    X(size_t, grove_index_in_parent, (const grove_element* element), (element)) \
    X(bool, grove_is_attached, (grove_document* document, const grove_element* element), (document, element)) \
    X(grove_element*, grove_create_element, (grove_document* document, const char* tag), (document, tag)) \
    X(bool, grove_append_child, (grove_document* document, grove_element* parent, grove_element* child), (document, parent, child)) \
    X(bool, grove_insert_before, (grove_document* document, grove_element* parent, grove_element* child, grove_element* reference), (document, parent, child, reference)) \
    X(bool, grove_remove_element, (grove_document* document, grove_element* element), (document, element)) \
    X(bool, grove_has_class, (const grove_element* element, const char* name), (element, name)) \
    X(void, grove_add_class, (grove_document* document, grove_element* element, const char* name), (document, element, name)) \
    X(void, grove_remove_class, (grove_document* document, grove_element* element, const char* name), (document, element, name)) \
    X(bool, grove_toggle_class, (grove_document* document, grove_element* element, const char* name), (document, element, name)) \
    X(const char*, grove_get_style, (const grove_element* element, const char* property), (element, property)) \
    X(void, grove_set_style, (grove_document* document, grove_element* element, const char* property, const char* value), (document, element, property, value)) \
    X(const char*, grove_computed_style, (grove_document* document, grove_element* element, const char* property), (document, element, property)) \
    X(bool, grove_focus, (grove_document* document, grove_element* element), (document, element)) \
    X(bool, grove_has_state, (const grove_element* element, const char* state), (element, state)) \
    X(uint64_t, grove_add_event_listener, (grove_document* document, grove_element* element, const char* type, grove_listener_fn fn, void* user_data, grove_release_fn release), (document, element, type, fn, user_data, release)) \
    X(bool, grove_remove_event_listener, (grove_document* document, uint64_t id), (document, id)) \
    X(bool, grove_dispatch_event, (grove_document* document, grove_element* target, const char* type), (document, target, type)) \
    X(grove_event*, grove_current_event, (grove_document* document), (document)) \
    X(const char*, grove_event_type, (const grove_event* event), (event)) \
    X(grove_element*, grove_event_target, (grove_event* event), (event)) \
    X(grove_element*, grove_event_current_target, (grove_event* event), (event)) \
    X(double, grove_event_x, (const grove_event* event), (event)) \
    X(double, grove_event_y, (const grove_event* event), (event)) \
    X(double, grove_event_delta_x, (const grove_event* event), (event)) \
    X(double, grove_event_delta_y, (const grove_event* event), (event)) \
    X(int, grove_event_button, (const grove_event* event), (event)) \
    X(const char*, grove_event_key, (const grove_event* event), (event)) \
    X(const char*, grove_event_text, (const grove_event* event), (event)) \
    X(unsigned, grove_event_modifiers, (const grove_event* event), (event)) \
    X(void, grove_event_stop_propagation, (grove_event* event), (event)) \
    X(void, grove_event_prevent_default, (grove_event* event), (event)) \
    X(bool, grove_event_default_prevented, (const grove_event* event), (event)) \
    X(uint64_t, grove_set_timeout, (grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release), (document, milliseconds, fn, user_data, release)) \
    X(uint64_t, grove_set_interval, (grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release), (document, milliseconds, fn, user_data, release)) \
    X(bool, grove_clear_timer, (grove_document* document, uint64_t id), (document, id)) \
    X(double, grove_time_ms, (grove_document* document), (document)) \
    X(double, grove_viewport_width, (grove_document* document), (document)) \
    X(double, grove_viewport_height, (grove_document* document), (document)) \
    X(void, grove_quit, (grove_document* document), (document)) \
    X(void, grove_log, (const char* message), (message)) \
    X(grove_element*, grove_main_window, (grove_document* document), (document)) \
    X(grove_element*, grove_window_of, (grove_document* document, grove_element* element), (document, element)) \
    X(bool, grove_window_open, (grove_document* document, grove_element* window), (document, window)) \
    X(bool, grove_window_close, (grove_document* document, grove_element* window), (document, window)) \
    X(bool, grove_window_is_open, (grove_document* document, grove_element* window), (document, window)) \
    X(double, grove_window_width, (grove_document* document, grove_element* window), (document, window)) \
    X(double, grove_window_height, (grove_document* document, grove_element* window), (document, window)) \
    X(bool, grove_window_resize, (grove_document* document, grove_element* window, double width, double height), (document, window, width, height)) \
    X(bool, grove_window_move, (grove_document* document, grove_element* window, double x, double y), (document, window, x, y)) \
    X(bool, grove_window_focus, (grove_document* document, grove_element* window), (document, window)) \
    X(grove_element*, grove_popup, (grove_document* document, grove_element* owner, const char* kind, const char* title, const char* message, const char* input, const char* value, const char* options), (document, owner, kind, title, message, input, value, options)) \
    X(bool, grove_popup_answer, (grove_document* document, grove_element* popup, bool ok), (document, popup, ok)) \
    X(const char*, grove_input_text, (const grove_element* element), (element)) \
    X(const char*, grove_call_native, (grove_document* document, const char* name, const char* const* args, size_t count), (document, name, args, count)) \
    X(bool, grove_has_native, (grove_document* document, const char* name), (document, name)) \
    X(const char*, grove_file_dialog, (grove_document* document, const char* mode, const char* title, const char* filter, bool multiple), (document, mode, title, filter, multiple)) \
    X(bool, grove_show_page, (grove_document* document, grove_element* page), (document, page)) \
    X(bool, grove_go_back, (grove_document* document), (document)) \
    X(bool, grove_open_modal, (grove_document* document, grove_element* modal), (document, modal)) \
    X(bool, grove_close_modal, (grove_document* document, grove_element* modal), (document, modal)) \
    X(bool, grove_navigate, (grove_document* document, const char* target), (document, target)) \
    X(bool, grove_is_current, (const grove_element* element), (element))

#endif // GROVE_API_LIST_H
