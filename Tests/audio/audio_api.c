#include <grove/api/grove_api.h>

void audio_c_start(void) {
    grove_document* doc = grove_current_document();
    grove_element* beep = grove_find_by_id(doc, "beep");
    int64_t fx = grove_effect_create("robot");
    int ok = fx > 0 && grove_effect_set(fx, "mix", 0.5) && grove_effect_free(fx);
    ok = ok && grove_media_set_volume(doc, beep, 0.8) && !grove_media_is_playing(doc, beep);
    ok = ok && grove_audio_device_count(false) > 0;
    grove_set_text_by_id("c", ok ? "c ok" : "c failed");
}
