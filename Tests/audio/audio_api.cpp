#include <grove/dom.hpp>

GROVE_ON_LOAD(audio_cpp) {
    grove::Element beep = grove::document.get_by_id("beep");
    const std::int64_t fx = grove::document.create_effect("reverb");
    const bool ok = fx > 0 && grove::document.effect_param_count(fx) > 0 && grove::document.free_effect(fx);
    const std::int64_t sound = grove::document.preload_sound("beep.wav");
    const bool sound_ok = sound > 0 && grove::document.unload_sound(sound);
    const bool media_ok = beep.set_volume(0.8) && beep.volume() > 0.79 && !beep.is_playing() && beep.media_error().empty();
    grove::document.get_by_id("cpp").set_text(ok && sound_ok && media_ok ? "cpp ok" : "cpp failed");
}
