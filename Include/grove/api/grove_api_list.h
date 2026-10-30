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
    X(bool, grove_has_computed_style, (grove_document* document, grove_element* element, const char* property), (document, element, property)) \
    X(double, grove_computed_number, (grove_document* document, grove_element* element, const char* property, double fallback), (document, element, property, fallback)) \
    X(double, grove_computed_number_at, (grove_document* document, grove_element* element, const char* property, size_t index, double fallback), (document, element, property, index, fallback)) \
    X(const char*, grove_computed_color, (grove_document* document, grove_element* element, const char* property), (document, element, property)) \
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
    X(void, grove_set_max_fps, (grove_document* document, double fps), (document, fps)) \
    X(double, grove_max_fps, (grove_document* document), (document)) \
    X(void, grove_set_idle_fps, (grove_document* document, double fps), (document, fps)) \
    X(double, grove_idle_fps, (grove_document* document), (document)) \
    X(void, grove_set_vsync, (grove_document* document, bool enabled), (document, enabled)) \
    X(bool, grove_vsync, (grove_document* document), (document)) \
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
    X(bool, grove_is_current, (const grove_element* element), (element)) \
    X(grove_bark_values*, grove_bark_values_new, (void), ()) \
    X(void, grove_bark_values_free, (grove_bark_values* values), (values)) \
    X(void, grove_bark_values_set, (grove_bark_values* values, const char* name, const char* value), (values, name, value)) \
    X(void, grove_bark_values_set_default, (grove_bark_values* values, const char* name, const char* value), (values, name, value)) \
    X(bool, grove_bark_values_has, (const grove_bark_values* values, const char* name), (values, name)) \
    X(const char*, grove_bark_values_get, (const grove_bark_values* values, const char* name), (values, name)) \
    X(void, grove_bark_values_erase, (grove_bark_values* values, const char* name), (values, name)) \
    X(void, grove_bark_values_provide, (grove_bark_values* values, const char* name, grove_bark_provider_fn fn, void* user_data, grove_release_fn release), (values, name, fn, user_data, release)) \
    X(void, grove_bark_values_set_parent, (grove_bark_values* values, const grove_bark_values* parent), (values, parent)) \
    X(size_t, grove_bark_values_count, (const grove_bark_values* values), (values)) \
    X(const char*, grove_bark_values_name, (const grove_bark_values* values, size_t index), (values, index)) \
    X(const char*, grove_bark_interpolate, (const char* text, const grove_bark_values* values), (text, values)) \
    X(size_t, grove_bark_unknown_count, (const char* text, const grove_bark_values* values), (text, values)) \
    X(const char*, grove_bark_unknown, (const char* text, const grove_bark_values* values, size_t index), (text, values, index)) \
    X(const char*, grove_bark_escape_braces, (const char* text), (text)) \
    X(grove_bark_file*, grove_bark_parse, (const char* text, const char* path), (text, path)) \
    X(grove_bark_file*, grove_bark_load, (const char* path), (path)) \
    X(void, grove_bark_file_free, (grove_bark_file* file), (file)) \
    X(size_t, grove_bark_error_count, (const grove_bark_file* file), (file)) \
    X(const char*, grove_bark_error, (const grove_bark_file* file, size_t index), (file, index)) \
    X(size_t, grove_bark_rule_count, (const grove_bark_file* file), (file)) \
    X(const char*, grove_bark_rule_name, (const grove_bark_file* file, size_t rule), (file, rule)) \
    X(int64_t, grove_bark_find_rule, (const grove_bark_file* file, const char* name), (file, name)) \
    X(size_t, grove_bark_property_count, (const grove_bark_file* file, size_t rule), (file, rule)) \
    X(const char*, grove_bark_property_name, (const grove_bark_file* file, size_t rule, size_t property), (file, rule, property)) \
    X(size_t, grove_bark_property_line, (const grove_bark_file* file, size_t rule, size_t property), (file, rule, property)) \
    X(const char*, grove_bark_property_text, (const grove_bark_file* file, size_t rule, size_t property), (file, rule, property)) \
    X(size_t, grove_bark_value_count, (const grove_bark_file* file, size_t rule, size_t property), (file, rule, property)) \
    X(const char*, grove_bark_value, (const grove_bark_file* file, size_t rule, size_t property, size_t index), (file, rule, property, index)) \
    X(const char*, grove_bark_setting, (const grove_bark_file* file, size_t rule, const char* name, const grove_bark_values* values), (file, rule, name, values)) \
    X(size_t, grove_bark_variable_count, (const grove_bark_file* file), (file)) \
    X(const char*, grove_bark_variable_name, (const grove_bark_file* file, size_t index), (file, index)) \
    X(const char*, grove_bark_variable_value, (const grove_bark_file* file, size_t index), (file, index)) \
    X(void, grove_bark_add_variables, (const grove_bark_file* file, grove_bark_values* values), (file, values)) \
    X(grove_bark_actions*, grove_bark_actions_new, (void), ()) \
    X(void, grove_bark_actions_free, (grove_bark_actions* actions), (actions)) \
    X(void, grove_bark_actions_add, (grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_action_fn fn, void* user_data, grove_release_fn release), (actions, name, min_values, max_values, flags, fn, user_data, release)) \
    X(void, grove_bark_actions_setting, (grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values), (actions, name, min_values, max_values)) \
    X(void, grove_bark_actions_add_standard, (grove_bark_actions* actions), (actions)) \
    X(bool, grove_bark_actions_has, (const grove_bark_actions* actions, const char* name), (actions, name)) \
    X(bool, grove_bark_actions_remove, (grove_bark_actions* actions, const char* name), (actions, name)) \
    X(size_t, grove_bark_check_count, (const grove_bark_actions* actions, const grove_bark_file* file, size_t rule), (actions, file, rule)) \
    X(const char*, grove_bark_check, (const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t index), (actions, file, rule, index)) \
    X(const char*, grove_bark_call_name, (const grove_bark_call* call), (call)) \
    X(size_t, grove_bark_call_arg_count, (const grove_bark_call* call), (call)) \
    X(const char*, grove_bark_call_arg, (const grove_bark_call* call, size_t index), (call, index)) \
    X(const char*, grove_bark_call_rest, (const grove_bark_call* call, size_t from), (call, from)) \
    X(size_t, grove_bark_call_line, (const grove_bark_call* call), (call)) \
    X(grove_bark_values*, grove_bark_call_values, (grove_bark_call* call), (call)) \
    X(grove_bark_runner*, grove_bark_call_runner, (grove_bark_call* call), (call)) \
    X(int, grove_bark_call_fail, (grove_bark_call* call, const char* message), (call, message)) \
    X(void, grove_bark_actions_add_function, (grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_function_fn fn, void* user_data, grove_release_fn release), (actions, name, min_values, max_values, flags, fn, user_data, release)) \
    X(void, grove_bark_actions_add_standard_functions, (grove_bark_actions* actions), (actions)) \
    X(bool, grove_bark_actions_has_function, (const grove_bark_actions* actions, const char* name), (actions, name)) \
    X(bool, grove_bark_actions_remove_function, (grove_bark_actions* actions, const char* name), (actions, name)) \
    X(const char*, grove_bark_evaluate_setting, (const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const char* name, grove_bark_values* values), (actions, file, rule, name, values)) \
    X(const char*, grove_bark_evaluate_value, (const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t property, size_t index, grove_bark_values* values), (actions, file, rule, property, index, values)) \
    X(const char*, grove_bark_evaluate_error, (void), ()) \
    X(grove_bark_runner*, grove_bark_runner_new, (const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const grove_bark_values* parent), (actions, file, rule, parent)) \
    X(void, grove_bark_runner_free, (grove_bark_runner* runner), (runner)) \
    X(int, grove_bark_runner_run, (grove_bark_runner* runner), (runner)) \
    X(int, grove_bark_runner_step, (grove_bark_runner* runner), (runner)) \
    X(int, grove_bark_runner_resume, (grove_bark_runner* runner), (runner)) \
    X(int, grove_bark_runner_answer, (grove_bark_runner* runner, const char* name, const char* value), (runner, name, value)) \
    X(void, grove_bark_runner_stop, (grove_bark_runner* runner), (runner)) \
    X(int, grove_bark_runner_state, (const grove_bark_runner* runner), (runner)) \
    X(const char*, grove_bark_runner_error, (const grove_bark_runner* runner), (runner)) \
    X(grove_bark_values*, grove_bark_runner_values, (grove_bark_runner* runner), (runner)) \
    X(size_t, grove_bark_runner_position, (const grove_bark_runner* runner), (runner)) \
    X(size_t, grove_bark_runner_size, (const grove_bark_runner* runner), (runner))

#endif // GROVE_API_LIST_H
