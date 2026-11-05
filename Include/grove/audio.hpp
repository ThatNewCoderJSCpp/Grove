#ifndef GROVE_AUDIO_HPP
#define GROVE_AUDIO_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fizmo {
namespace audio {
class AudioBuffer;
class Effect;
class Mixer;
} // namespace audio
} // namespace fizmo

namespace grove {
namespace audio {

struct Device {
    std::string id;
    std::string name;
    bool is_default = false;
};

bool supported() noexcept;
std::string backend();
std::vector<Device> output_devices();
std::vector<Device> input_devices();
bool set_output_device(const std::string& id, std::string* error = nullptr);
std::string output_device();
bool open_output(std::string* error = nullptr);
bool output_open() noexcept;
std::string last_error();

void set_master_volume(double volume);
double master_volume();
double master_level();

void set_manual_output(bool manual);
bool manual_output() noexcept;
void advance(double seconds);
void shutdown();

std::uint32_t mixer_rate() noexcept;
std::shared_ptr<fizmo::audio::Mixer> shared_mixer();

struct Clip;

struct ClipStatus {
    bool done = false;
    bool failed = false;
    bool has_info = false;
    double duration = 0.0;
    std::string error;
};

std::shared_ptr<Clip> load_clip(const std::string& path, bool decode);
std::shared_ptr<Clip> load_clip(const std::string& path, bool decode, bool allow_stream);
void decode_clip(const std::shared_ptr<Clip>& clip);
void cancel_clip(const std::shared_ptr<Clip>& clip);
ClipStatus clip_status(const Clip& clip);
std::shared_ptr<const fizmo::audio::AudioBuffer> clip_buffer(const Clip& clip);
bool clip_streams(const Clip& clip);

struct VoiceOptions {
    double gain = 1.0;
    double pan = 0.0;
    double speed = 1.0;
    bool loop = false;
    bool paused = false;
    bool preserve_pitch = false;
    double start = 0.0;
    double clip_start = 0.0;
    double clip_end = -1.0;
};

std::uint64_t start_voice(const std::shared_ptr<Clip>& clip, const VoiceOptions& options, std::string* error = nullptr);
void stop_voice(std::uint64_t voice, double fade = 0.0);
void pause_voice(std::uint64_t voice, bool paused);
bool seek_voice(std::uint64_t voice, double seconds);
void set_voice_gain(std::uint64_t voice, double gain);
void set_voice_pan(std::uint64_t voice, double pan);
void set_voice_speed(std::uint64_t voice, double speed);
void set_voice_loop(std::uint64_t voice, bool loop);
bool voice_active(std::uint64_t voice);
double voice_position(std::uint64_t voice);

std::int64_t preload_sound(const std::string& path);
bool sound_loaded(std::int64_t sound);
std::string sound_error(std::int64_t sound);
double sound_duration(std::int64_t sound);
bool unload_sound(std::int64_t sound);
std::int64_t play_sound(std::int64_t sound, double volume = 1.0, double pan = 0.0, bool loop = false);
std::int64_t play_sound(const std::string& path, double volume = 1.0, double pan = 0.0, bool loop = false);
bool stop_sound(std::int64_t id, double fade = 0.0);
bool set_sound_volume(std::int64_t id, double volume);
bool set_sound_pan(std::int64_t id, double pan);
bool sound_playing(std::int64_t id);
void stop_all_sounds(double fade = 0.0);
void update_sounds();

std::vector<std::string> effect_types();
std::int64_t create_effect(const std::string& type);
std::shared_ptr<fizmo::audio::Effect> effect(std::int64_t handle);
std::int64_t add_effect(std::shared_ptr<fizmo::audio::Effect> effect);
bool free_effect(std::int64_t handle);
std::string effect_type(std::int64_t handle);
bool set_effect_param(std::int64_t handle, const std::string& name, double value);
double effect_param(std::int64_t handle, const std::string& name);
std::vector<std::string> effect_params(std::int64_t handle);
bool set_effect_enabled(std::int64_t handle, bool enabled);
bool chain_add(std::int64_t chain, std::int64_t effect);
bool chain_remove(std::int64_t chain, std::size_t index);
bool chain_clear(std::int64_t chain);
std::size_t chain_size(std::int64_t chain);

std::int64_t start_monitor(const std::string& input_device, const std::string& output_device, std::int64_t effect, std::string* error = nullptr);
bool stop_monitor(std::int64_t monitor);
bool monitor_running(std::int64_t monitor);
bool set_monitor_effect(std::int64_t monitor, std::int64_t effect);
bool set_monitor_volume(std::int64_t monitor, double volume);
double monitor_input_level(std::int64_t monitor);
double monitor_output_level(std::int64_t monitor);
void stop_all_monitors();
bool monitor_waiting(std::int64_t monitor);
std::string monitor_error(std::int64_t monitor);
std::vector<std::int64_t> update_monitors();

double level_to_db(double level);

} // namespace audio
} // namespace grove

#endif // GROVE_AUDIO_HPP
