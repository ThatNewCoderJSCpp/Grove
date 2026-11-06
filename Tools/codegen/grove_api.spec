grove_element*|grove_root|grove_document* document
grove_element*|grove_find_by_id|grove_document* document, const char* id
grove_element*|grove_first_by_tag|grove_document* document, const char* tag
const char*|grove_tag|const grove_element* element
grove_element*|grove_parent|grove_element* element
size_t|grove_child_count|const grove_element* element
grove_element*|grove_child|grove_element* element, size_t index
int|grove_has_attribute|const grove_element* element, const char* name
const char*|grove_get_attribute|const grove_element* element, const char* name
void|grove_set_attribute|grove_document* document, grove_element* element, const char* name, const char* value
int|grove_remove_attribute|grove_document* document, grove_element* element, const char* name
size_t|grove_get_text|const grove_element* element, char* buffer, size_t size
void|grove_set_text|grove_document* document, grove_element* element, const char* text
unsigned long long|grove_revision|const grove_document* document
void|grove_bind_document|grove_document* document
grove_document*|grove_current_document|void
int|grove_set_text_by_id|const char* id, const char* text
const char*|grove_get_attribute_by_id|const char* id, const char* name
int|grove_set_attribute_by_id|const char* id, const char* name, const char* value
int|grove_input_kind_of|const grove_element* element
bool|grove_input_integer|const grove_element* element, int64_t* out
bool|grove_input_float|const grove_element* element, double* out
bool|grove_input_bool|const grove_element* element, bool* out
const char*|grove_input_string|const grove_element* element
bool|grove_input_color|const grove_element* element, uint32_t* rgba
bool|grove_set_input_integer|grove_document* document, grove_element* element, int64_t value
bool|grove_set_input_float|grove_document* document, grove_element* element, double value
bool|grove_set_input_bool|grove_document* document, grove_element* element, bool value
bool|grove_set_input_string|grove_document* document, grove_element* element, const char* value
bool|grove_set_input_color|grove_document* document, grove_element* element, uint32_t rgba
const char*|grove_radio_choice|grove_document* document, const char* group
bool|grove_set_radio_choice|grove_document* document, const char* group, const char* value
int|grove_input_kind_by_id|const char* id
int64_t|grove_get_integer_by_id|const char* id
double|grove_get_float_by_id|const char* id
bool|grove_get_bool_by_id|const char* id
const char*|grove_get_string_by_id|const char* id
uint32_t|grove_get_color_by_id|const char* id
bool|grove_set_integer_by_id|const char* id, int64_t value
bool|grove_set_float_by_id|const char* id, double value
bool|grove_set_bool_by_id|const char* id, bool value
bool|grove_set_string_by_id|const char* id, const char* value
bool|grove_set_color_by_id|const char* id, uint32_t rgba
const char*|grove_get_choice|const char* group
bool|grove_set_choice|const char* group, const char* value
grove_element*|grove_body|grove_document* document
grove_element*|grove_query|grove_document* document, grove_element* scope, const char* selector
size_t|grove_query_all|grove_document* document, grove_element* scope, const char* selector, grove_element** out, size_t capacity
const char*|grove_id|const grove_element* element
const char*|grove_text|const grove_element* element
size_t|grove_index_in_parent|const grove_element* element
bool|grove_is_attached|grove_document* document, const grove_element* element
grove_element*|grove_create_element|grove_document* document, const char* tag
bool|grove_append_child|grove_document* document, grove_element* parent, grove_element* child
bool|grove_insert_before|grove_document* document, grove_element* parent, grove_element* child, grove_element* reference
bool|grove_remove_element|grove_document* document, grove_element* element
bool|grove_has_class|const grove_element* element, const char* name
void|grove_add_class|grove_document* document, grove_element* element, const char* name
void|grove_remove_class|grove_document* document, grove_element* element, const char* name
bool|grove_toggle_class|grove_document* document, grove_element* element, const char* name
const char*|grove_get_style|const grove_element* element, const char* property
void|grove_set_style|grove_document* document, grove_element* element, const char* property, const char* value
const char*|grove_computed_style|grove_document* document, grove_element* element, const char* property
bool|grove_has_computed_style|grove_document* document, grove_element* element, const char* property
double|grove_computed_number|grove_document* document, grove_element* element, const char* property, double fallback
double|grove_computed_number_at|grove_document* document, grove_element* element, const char* property, size_t index, double fallback
const char*|grove_computed_color|grove_document* document, grove_element* element, const char* property
bool|grove_focus|grove_document* document, grove_element* element
bool|grove_has_state|const grove_element* element, const char* state
uint64_t|grove_add_event_listener|grove_document* document, grove_element* element, const char* type, grove_listener_fn fn, void* user_data, grove_release_fn release
bool|grove_remove_event_listener|grove_document* document, uint64_t id
bool|grove_dispatch_event|grove_document* document, grove_element* target, const char* type
grove_event*|grove_current_event|grove_document* document
const char*|grove_event_type|const grove_event* event
grove_element*|grove_event_target|grove_event* event
grove_element*|grove_event_current_target|grove_event* event
double|grove_event_x|const grove_event* event
double|grove_event_y|const grove_event* event
double|grove_event_delta_x|const grove_event* event
double|grove_event_delta_y|const grove_event* event
int|grove_event_button|const grove_event* event
const char*|grove_event_key|const grove_event* event
const char*|grove_event_text|const grove_event* event
unsigned|grove_event_modifiers|const grove_event* event
void|grove_event_stop_propagation|grove_event* event
void|grove_event_prevent_default|grove_event* event
bool|grove_event_default_prevented|const grove_event* event
uint64_t|grove_set_timeout|grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release
uint64_t|grove_set_interval|grove_document* document, double milliseconds, grove_timer_fn fn, void* user_data, grove_release_fn release
bool|grove_clear_timer|grove_document* document, uint64_t id
double|grove_time_ms|grove_document* document
double|grove_viewport_width|grove_document* document
double|grove_viewport_height|grove_document* document
void|grove_quit|grove_document* document
void|grove_set_max_fps|grove_document* document, double fps
double|grove_max_fps|grove_document* document
void|grove_set_idle_fps|grove_document* document, double fps
double|grove_idle_fps|grove_document* document
void|grove_set_vsync|grove_document* document, bool enabled
bool|grove_vsync|grove_document* document
void|grove_log|const char* message
grove_element*|grove_main_window|grove_document* document
grove_element*|grove_window_of|grove_document* document, grove_element* element
bool|grove_window_open|grove_document* document, grove_element* window
bool|grove_window_close|grove_document* document, grove_element* window
bool|grove_window_is_open|grove_document* document, grove_element* window
double|grove_window_width|grove_document* document, grove_element* window
double|grove_window_height|grove_document* document, grove_element* window
bool|grove_window_resize|grove_document* document, grove_element* window, double width, double height
bool|grove_window_move|grove_document* document, grove_element* window, double x, double y
bool|grove_window_focus|grove_document* document, grove_element* window
grove_element*|grove_popup|grove_document* document, grove_element* owner, const char* kind, const char* title, const char* message, const char* input, const char* value, const char* options
bool|grove_popup_answer|grove_document* document, grove_element* popup, bool ok
const char*|grove_input_text|const grove_element* element
const char*|grove_call_native|grove_document* document, const char* name, const char* const* args, size_t count
bool|grove_has_native|grove_document* document, const char* name
const char*|grove_file_dialog|grove_document* document, const char* mode, const char* title, const char* filter, bool multiple
bool|grove_show_page|grove_document* document, grove_element* page
bool|grove_go_back|grove_document* document
bool|grove_open_modal|grove_document* document, grove_element* modal
bool|grove_close_modal|grove_document* document, grove_element* modal
bool|grove_navigate|grove_document* document, const char* target
bool|grove_is_current|const grove_element* element
grove_bark_values*|grove_bark_values_new|void
void|grove_bark_values_free|grove_bark_values* values
void|grove_bark_values_set|grove_bark_values* values, const char* name, const char* value
void|grove_bark_values_set_default|grove_bark_values* values, const char* name, const char* value
bool|grove_bark_values_has|const grove_bark_values* values, const char* name
const char*|grove_bark_values_get|const grove_bark_values* values, const char* name
void|grove_bark_values_erase|grove_bark_values* values, const char* name
void|grove_bark_values_provide|grove_bark_values* values, const char* name, grove_bark_provider_fn fn, void* user_data, grove_release_fn release
void|grove_bark_values_set_parent|grove_bark_values* values, const grove_bark_values* parent
size_t|grove_bark_values_count|const grove_bark_values* values
const char*|grove_bark_values_name|const grove_bark_values* values, size_t index
const char*|grove_bark_interpolate|const char* text, const grove_bark_values* values
size_t|grove_bark_unknown_count|const char* text, const grove_bark_values* values
const char*|grove_bark_unknown|const char* text, const grove_bark_values* values, size_t index
const char*|grove_bark_escape_braces|const char* text
grove_bark_file*|grove_bark_parse|const char* text, const char* path
grove_bark_file*|grove_bark_load|const char* path
void|grove_bark_file_free|grove_bark_file* file
size_t|grove_bark_error_count|const grove_bark_file* file
const char*|grove_bark_error|const grove_bark_file* file, size_t index
size_t|grove_bark_rule_count|const grove_bark_file* file
const char*|grove_bark_rule_name|const grove_bark_file* file, size_t rule
int64_t|grove_bark_find_rule|const grove_bark_file* file, const char* name
size_t|grove_bark_property_count|const grove_bark_file* file, size_t rule
const char*|grove_bark_property_name|const grove_bark_file* file, size_t rule, size_t property
size_t|grove_bark_property_line|const grove_bark_file* file, size_t rule, size_t property
const char*|grove_bark_property_text|const grove_bark_file* file, size_t rule, size_t property
size_t|grove_bark_value_count|const grove_bark_file* file, size_t rule, size_t property
const char*|grove_bark_value|const grove_bark_file* file, size_t rule, size_t property, size_t index
const char*|grove_bark_setting|const grove_bark_file* file, size_t rule, const char* name, const grove_bark_values* values
size_t|grove_bark_variable_count|const grove_bark_file* file
const char*|grove_bark_variable_name|const grove_bark_file* file, size_t index
const char*|grove_bark_variable_value|const grove_bark_file* file, size_t index
void|grove_bark_add_variables|const grove_bark_file* file, grove_bark_values* values
grove_bark_actions*|grove_bark_actions_new|void
void|grove_bark_actions_free|grove_bark_actions* actions
void|grove_bark_actions_add|grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_action_fn fn, void* user_data, grove_release_fn release
void|grove_bark_actions_setting|grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values
void|grove_bark_actions_add_standard|grove_bark_actions* actions
bool|grove_bark_actions_has|const grove_bark_actions* actions, const char* name
bool|grove_bark_actions_remove|grove_bark_actions* actions, const char* name
size_t|grove_bark_check_count|const grove_bark_actions* actions, const grove_bark_file* file, size_t rule
const char*|grove_bark_check|const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t index
const char*|grove_bark_call_name|const grove_bark_call* call
size_t|grove_bark_call_arg_count|const grove_bark_call* call
const char*|grove_bark_call_arg|const grove_bark_call* call, size_t index
const char*|grove_bark_call_rest|const grove_bark_call* call, size_t from
size_t|grove_bark_call_line|const grove_bark_call* call
grove_bark_values*|grove_bark_call_values|grove_bark_call* call
grove_bark_runner*|grove_bark_call_runner|grove_bark_call* call
int|grove_bark_call_fail|grove_bark_call* call, const char* message
void|grove_bark_actions_add_function|grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_function_fn fn, void* user_data, grove_release_fn release
void|grove_bark_actions_add_standard_functions|grove_bark_actions* actions
bool|grove_bark_actions_has_function|const grove_bark_actions* actions, const char* name
bool|grove_bark_actions_remove_function|grove_bark_actions* actions, const char* name
const char*|grove_bark_evaluate_setting|const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const char* name, grove_bark_values* values
const char*|grove_bark_evaluate_value|const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t property, size_t index, grove_bark_values* values
const char*|grove_bark_evaluate_error|void
grove_bark_runner*|grove_bark_runner_new|const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const grove_bark_values* parent
void|grove_bark_runner_free|grove_bark_runner* runner
int|grove_bark_runner_run|grove_bark_runner* runner
int|grove_bark_runner_step|grove_bark_runner* runner
int|grove_bark_runner_resume|grove_bark_runner* runner
int|grove_bark_runner_answer|grove_bark_runner* runner, const char* name, const char* value
void|grove_bark_runner_stop|grove_bark_runner* runner
int|grove_bark_runner_state|const grove_bark_runner* runner
const char*|grove_bark_runner_error|const grove_bark_runner* runner
grove_bark_values*|grove_bark_runner_values|grove_bark_runner* runner
size_t|grove_bark_runner_position|const grove_bark_runner* runner
size_t|grove_bark_runner_size|const grove_bark_runner* runner
const char*|grove_file_dialog_checked|grove_document* document, const char* mode, const char* title, const char* filter, const char* reject, bool enforce, bool multiple
bool|grove_media_play|grove_document* document, grove_element* element
bool|grove_media_pause|grove_document* document, grove_element* element
bool|grove_media_stop|grove_document* document, grove_element* element
bool|grove_media_seek|grove_document* document, grove_element* element, double seconds
double|grove_media_position|grove_document* document, grove_element* element
double|grove_media_duration|grove_document* document, grove_element* element
bool|grove_media_is_playing|grove_document* document, grove_element* element
bool|grove_media_set_volume|grove_document* document, grove_element* element, double volume
bool|grove_media_set_muted|grove_document* document, grove_element* element, bool muted
bool|grove_media_set_loop|grove_document* document, grove_element* element, bool loop
bool|grove_media_set_rate|grove_document* document, grove_element* element, double rate
bool|grove_media_set_src|grove_document* document, grove_element* element, const char* src
const char*|grove_media_error|grove_document* document, grove_element* element
double|grove_media_level|grove_document* document, grove_element* element
int64_t|grove_sound_preload|grove_document* document, const char* path
bool|grove_sound_loaded|int64_t sound
bool|grove_sound_unload|int64_t sound
int64_t|grove_sound_play|grove_document* document, const char* path, double volume, double pan, bool loop
int64_t|grove_sound_play_preloaded|int64_t sound, double volume, double pan, bool loop
bool|grove_sound_stop|int64_t id
bool|grove_sound_set_volume|int64_t id, double volume
bool|grove_sound_is_playing|int64_t id
void|grove_sound_stop_all|void
void|grove_audio_set_master_volume|double volume
double|grove_audio_master_volume|void
double|grove_audio_master_level|void
size_t|grove_audio_device_count|bool input
const char*|grove_audio_device_id|bool input, size_t index
const char*|grove_audio_device_name|bool input, size_t index
bool|grove_audio_set_output_device|const char* id
const char*|grove_audio_output_device|void
const char*|grove_audio_error|void
int64_t|grove_effect_create|const char* type
bool|grove_effect_free|int64_t effect
bool|grove_effect_set|int64_t effect, const char* name, double value
double|grove_effect_get|int64_t effect, const char* name
size_t|grove_effect_param_count|int64_t effect
const char*|grove_effect_param_name|int64_t effect, size_t index
bool|grove_effect_set_enabled|int64_t effect, bool enabled
bool|grove_effect_chain_add|int64_t chain, int64_t effect
bool|grove_effect_chain_remove|int64_t chain, size_t index
int64_t|grove_monitor_start|const char* input_device, const char* output_device, int64_t effect
bool|grove_monitor_stop|int64_t monitor
bool|grove_monitor_set_effect|int64_t monitor, int64_t effect
double|grove_monitor_input_level|int64_t monitor
double|grove_monitor_output_level|int64_t monitor
double|grove_video_width|grove_document* document, grove_element* element
double|grove_video_height|grove_document* document, grove_element* element
bool|grove_video_set_fullscreen|grove_document* document, grove_element* element, bool fullscreen
bool|grove_video_is_fullscreen|grove_document* document, grove_element* element
size_t|grove_video_snapshot|grove_document* document, grove_element* element, uint8_t* rgba, size_t capacity, uint32_t* width, uint32_t* height
bool|grove_video_snapshot_to_canvas|grove_document* document, grove_element* element, grove_element* canvas
