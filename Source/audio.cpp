#include "grove/audio.hpp"
#include "grove/project.hpp"

#include <fizmo/../../Source/Audio/audio.hpp>
#include <fizmo/../../Source/Audio/presets.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace grove {
namespace audio {

namespace fa = fizmo::audio;

struct Clip {
    std::string path;
    bool allow_stream = false;
    bool stream = false;
    std::atomic<bool> cancel{ false };
    mutable std::mutex mutex;
    bool done = false;
    bool failed = false;
    bool has_info = false;
    bool queued = false;
    double duration = 0.0;
    std::string error;
    fa::Sound sound;
    double slice_start = -1.0;
    double slice_end = -1.0;
    fa::Sound slice;
    std::vector<std::int64_t> waiting;
};

namespace {

constexpr double k_stream_seconds = 30.0;

class Ring {
public:
    void reset(std::size_t samples) {
        m_data.assign(samples + 1, 0.0f);
        m_read.store(0);
        m_write.store(0);
    }

    std::size_t available() const noexcept {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return w >= r ? w - r : m_data.size() - r + w;
    }

    std::size_t space() const noexcept { return m_data.empty() ? 0 : m_data.size() - 1 - available(); }

    std::size_t push(const float* in, std::size_t count) noexcept {
        const std::size_t n = std::min(count, space());
        std::size_t w = m_write.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            m_data[w] = in[i];
            w = w + 1 == m_data.size() ? 0 : w + 1;
        }
        m_write.store(w, std::memory_order_release);
        return n;
    }

    std::size_t pop(float* out, std::size_t count) noexcept {
        const std::size_t n = std::min(count, available());
        std::size_t r = m_read.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            if (out) out[i] = m_data[r];
            r = r + 1 == m_data.size() ? 0 : r + 1;
        }
        m_read.store(r, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> m_data;
    std::atomic<std::size_t> m_read{ 0 };
    std::atomic<std::size_t> m_write{ 0 };
};

struct Monitor {
    fa::Stream capture;
    fa::Stream playback;
    std::shared_ptr<fa::EffectChain> chain = std::make_shared<fa::EffectChain>();
    std::int64_t effect = 0;
    Ring ring;
    std::vector<float> scratch;
    std::size_t in_channels = 1;
    std::size_t out_channels = 2;
    std::size_t max_backlog = 0;
    std::atomic<float> volume{ 1.0f };
    std::atomic<float> in_level{ 0.0f };
    std::atomic<float> out_level{ 0.0f };
    std::string input_device;
    std::string output_device;
    bool waiting = false;
    bool failed = false;
    std::string error;

    void on_capture(const float* in, std::size_t frames) noexcept {
        const std::size_t oc = out_channels;
        const std::size_t ic = std::max<std::size_t>(1, in_channels);
        float in_peak = 0.0f;
        float out_peak = 0.0f;
        const float gain = volume.load(std::memory_order_relaxed);
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t n = std::min(frames - done, scratch.size() / std::max<std::size_t>(1, oc));
            if (n == 0) break;
            for (std::size_t f = 0; f < n; ++f) {
                const float* src = in + (done + f) * ic;
                float mono = 0.0f;
                for (std::size_t c = 0; c < ic; ++c) {
                    in_peak = std::max(in_peak, std::fabs(src[c]));
                    mono += src[c];
                }
                mono /= static_cast<float>(ic);
                for (std::size_t c = 0; c < oc; ++c) scratch[f * oc + c] = ic == oc ? src[c] : (ic > 1 && c < ic ? src[c] : mono);
            }
            chain->process(scratch.data(), n);
            for (std::size_t i = 0; i < n * oc; ++i) {
                scratch[i] *= gain;
                out_peak = std::max(out_peak, std::fabs(scratch[i]));
            }
            ring.push(scratch.data(), n * oc);
            done += n;
        }
        in_level.store(in_peak, std::memory_order_relaxed);
        out_level.store(out_peak, std::memory_order_relaxed);
    }

    void on_playback(float* out, std::size_t frames) noexcept {
        const std::size_t oc = out_channels;
        const std::size_t backlog = ring.available();
        if (backlog > max_backlog + frames * oc) {
            const std::size_t drop = backlog - max_backlog;
            ring.pop(nullptr, drop - drop % oc);
        }
        const std::size_t got = ring.pop(out, frames * oc);
        if (got < frames * oc) std::memset(out + got, 0, (frames * oc - got) * sizeof(float));
    }
};

struct PlayingSound {
    std::shared_ptr<Clip> clip;
    std::uint64_t voice = 0;
    bool pending = true;
    double volume = 1.0;
    double pan = 0.0;
    bool loop = false;
};

struct SoundEntry {
    std::shared_ptr<Clip> clip;
    std::string path;
};

struct EffectEntry {
    std::shared_ptr<fa::Effect> effect;
    std::string type;
};

class Engine {
public:
    Engine() : m_mixer(48000, 2) {
        m_loader = std::thread([this] { loader_loop(); });
    }

    ~Engine() { stop(); }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_stopping) return;
            m_stopping = true;
            for (const auto& job : m_queue) job.first->cancel.store(true);
        }
        m_queue_cv.notify_all();
        if (m_loader.joinable()) m_loader.join();
        std::map<std::int64_t, std::unique_ptr<Monitor>> monitors;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            monitors.swap(m_monitors);
        }
        for (auto& kv : monitors) {
            kv.second->capture.close();
            kv.second->playback.close();
        }
        m_mixer.stop_all();
        m_mixer.stop_output();
    }

    fa::Mixer& mixer() noexcept { return m_mixer; }
    std::mutex& mutex() noexcept { return m_mutex; }

    void enqueue(const std::shared_ptr<Clip>& clip, bool decode) {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_stopping) return;
            m_queue.emplace_back(clip, decode);
        }
        m_queue_cv.notify_one();
    }

    bool open_output(std::string* error) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return open_output_locked(error);
    }

    bool open_output_locked(std::string* error) {
        if (m_manual) return true;
        if (m_mixer.output_running()) return true;
        fa::StreamConfig config;
        config.device = m_device;
        config.name = "grove";
        config.latency_ms = 30;
        std::string why;
        if (!m_mixer.start(config, &why)) {
            m_error = why.empty() ? std::string("can't open the audio output") : why;
            if (error) *error = m_error;
            return false;
        }
        return true;
    }

    bool set_device(const std::string& id, std::string* error) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool was_running = m_mixer.output_running();
        const std::string old = m_device;
        m_device = id;
        if (!was_running || m_manual) return true;
        m_mixer.stop_output();
        if (open_output_locked(error)) return true;
        m_device = old;
        std::string ignored;
        open_output_locked(&ignored);
        return false;
    }

    std::string device() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_device;
    }

    void set_error(const std::string& e) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = e;
    }

    std::string error() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_error;
    }

    void set_error_unlocked(const std::string& e) { m_error = e; }

    void set_manual(bool manual) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_manual = manual;
        if (manual) m_mixer.stop_output();
    }

    bool manual() const noexcept { return m_manual; }

    void advance(double seconds) {
        if (!m_manual) return;
        std::size_t frames = static_cast<std::size_t>(std::llround(std::max(0.0, seconds) * static_cast<double>(m_mixer.sample_rate())));
        std::vector<float> buf(512 * m_mixer.channels());
        while (frames > 0) {
            const std::size_t n = std::min<std::size_t>(frames, 512);
            m_mixer.render(buf.data(), n);
            frames -= n;
        }
    }

    std::int64_t next_id() { return m_next++; }

    std::map<std::int64_t, SoundEntry> sounds;
    std::map<std::string, std::int64_t> sound_paths;
    std::map<std::int64_t, PlayingSound> playing;
    std::map<std::int64_t, EffectEntry> effects;
    std::map<std::int64_t, std::unique_ptr<Monitor>>& monitors() { return m_monitors; }

    void start_waiting(Clip& clip) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::int64_t> waiting;
        {
            std::lock_guard<std::mutex> clock(clip.mutex);
            waiting.swap(clip.waiting);
        }
        for (std::int64_t id : waiting) {
            auto it = playing.find(id);
            if (it == playing.end() || !it->second.pending) continue;
            PlayingSound& p = it->second;
            p.pending = false;
            fa::Sound sound;
            {
                std::lock_guard<std::mutex> clock(clip.mutex);
                sound = clip.sound;
            }
            if (!sound) {
                std::lock_guard<std::mutex> clock(clip.mutex);
                if (!clip.error.empty()) m_error = clip.error;
                playing.erase(it);
                continue;
            }
            if (!open_output_locked(nullptr)) {
                playing.erase(it);
                continue;
            }
            fa::PlayOptions o;
            o.gain = static_cast<float>(p.volume);
            o.pan = static_cast<float>(p.pan);
            o.loop = p.loop;
            p.voice = m_mixer.play(sound, o);
            if (!p.voice) playing.erase(it);
        }
    }

private:
    void loader_loop() {
        for (;;) {
            std::pair<std::shared_ptr<Clip>, bool> job;
            {
                std::unique_lock<std::mutex> lock(m_queue_mutex);
                m_queue_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                if (m_stopping) return;
                job = std::move(m_queue.front());
                m_queue.pop_front();
            }
            run(*job.first, job.second);
            if (job.second) start_waiting(*job.first);
        }
    }

    void run(Clip& clip, bool decode) {
        auto fail = [&clip](const std::string& message) {
            std::lock_guard<std::mutex> lock(clip.mutex);
            clip.failed = true;
            clip.done = true;
            clip.queued = false;
            clip.error = message;
        };
        if (clip.cancel.load()) {
            fail("cancelled");
            return;
        }
        fa::AudioReader reader;
        std::string data;
        std::string why;
        bool ok = false;
        if (is_virtual_file(clip.path)) {
            const std::optional<std::string> bytes = read_file(clip.path);
            if (bytes) {
                data = *bytes;
                ok = reader.open(data.data(), data.size(), &why);
            } else {
                why = "can't read it";
            }
        } else if (!path_exists(clip.path)) {
            why = "there's no such file";
        } else {
            ok = reader.open(clip.path, &why);
        }
        if (!ok) {
            fail("can't play '" + clip.path + "': " + (why.empty() ? std::string("not an audio file Grove can read") : why));
            return;
        }
        const fa::AudioFileInfo info = reader.info();
        if (info.channels == 0 || info.sample_rate == 0) {
            fail("can't play '" + clip.path + "': it has no audio");
            return;
        }
        const double length = info.duration > 0.0 ? info.duration : (info.frames ? static_cast<double>(info.frames) / static_cast<double>(info.sample_rate) : 0.0);
        const bool stream = clip.allow_stream && data.empty() && length > k_stream_seconds;
        {
            std::lock_guard<std::mutex> lock(clip.mutex);
            clip.has_info = true;
            clip.duration = length;
            if (!decode) clip.queued = false;
            if (decode && stream) {
                clip.stream = true;
                clip.done = true;
                clip.queued = false;
            }
        }
        if (!decode || stream) return;
        fa::AudioBuffer buffer(info.channels, info.sample_rate);
        if (info.frames > 0 && info.frames < (std::uint64_t(1) << 31)) buffer.reserve(static_cast<std::size_t>(info.frames));
        for (;;) {
            if (clip.cancel.load() || m_stopping) {
                fail("cancelled");
                return;
            }
            const std::size_t got = reader.read(buffer, 32768);
            if (got == 0) break;
        }
        if (buffer.empty()) {
            fail("can't play '" + clip.path + "': it has no audio");
            return;
        }
        fa::Sound sound = fa::make_sound(std::move(buffer));
        std::lock_guard<std::mutex> lock(clip.mutex);
        clip.duration = sound->duration();
        clip.sound = std::move(sound);
        clip.done = true;
        clip.queued = false;
    }

    fa::Mixer m_mixer;
    std::mutex m_mutex;
    std::string m_device;
    std::string m_error;
    bool m_manual = false;
    std::int64_t m_next = 1;
    std::map<std::int64_t, std::unique_ptr<Monitor>> m_monitors;

    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::deque<std::pair<std::shared_ptr<Clip>, bool>> m_queue;
    std::atomic<bool> m_stopping{ false };
    std::thread m_loader;
};

std::shared_ptr<Engine>& engine_slot() {
    static std::shared_ptr<Engine>* slot = new std::shared_ptr<Engine>();
    return *slot;
}

std::mutex& engine_mutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}

bool g_manual = false;
bool g_exit_hook = false;

void shutdown_at_exit() {
    shutdown();
}

Engine& engine() {
    bool hook = false;
    Engine* made = nullptr;
    {
        std::lock_guard<std::mutex> lock(engine_mutex());
        std::shared_ptr<Engine>& slot = engine_slot();
        if (!slot) {
            fa::backend_name();
            slot = std::make_shared<Engine>();
            if (g_manual) slot->set_manual(true);
            hook = !g_exit_hook;
            g_exit_hook = true;
        }
        made = slot.get();
    }
    if (hook) std::atexit(shutdown_at_exit);
    return *made;
}

bool engine_exists() {
    std::lock_guard<std::mutex> lock(engine_mutex());
    return engine_slot() != nullptr;
}

std::vector<Device> list(fa::DeviceKind kind) {
    std::vector<Device> out;
    for (const fa::DeviceInfo& d : fa::devices(kind)) out.push_back(Device{ d.id, d.name, d.is_default });
    return out;
}

fa::Sound sound_for(Clip& clip, double start, double end) {
    std::lock_guard<std::mutex> lock(clip.mutex);
    if (!clip.sound) return nullptr;
    const double total = clip.sound->duration();
    const bool whole = start <= 0.0 && (end < 0.0 || end >= total);
    if (whole) return clip.sound;
    if (clip.slice && clip.slice_start == start && clip.slice_end == end) return clip.slice;
    const std::size_t from = clip.sound->frame_at(std::max(0.0, start));
    const std::size_t to = end < 0.0 ? clip.sound->frames() : std::min(clip.sound->frames(), clip.sound->frame_at(end));
    if (to <= from) return nullptr;
    clip.slice = fa::make_sound(clip.sound->slice(from, to - from));
    clip.slice_start = start;
    clip.slice_end = end;
    return clip.slice;
}

std::string effect_key(const std::string& raw) {
    std::string type;
    for (char c : raw) type += c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return type;
}

std::shared_ptr<fa::Effect> make_effect(const std::string& raw) {
    const std::string type = effect_key(raw);
    if (type.empty()) return nullptr;
    if (type != "filter" && fa::has_effect_type(type)) {
        if (std::shared_ptr<fa::Effect> made = fa::create_effect(type)) return made;
    }
    if (type == "chain") return std::make_shared<fa::EffectChain>();
    if (type == "gain") return std::make_shared<fa::Gain>();
    if (type == "filter" || type == "lowpass") return std::make_shared<fa::Filter>(fa::FilterType::lowpass);
    if (type == "highpass") return std::make_shared<fa::Filter>(fa::FilterType::highpass);
    if (type == "bandpass") return std::make_shared<fa::Filter>(fa::FilterType::bandpass);
    if (type == "notch") return std::make_shared<fa::Filter>(fa::FilterType::notch);
    if (type == "parametric_eq" || type == "eq") return std::make_shared<fa::ParametricEq>();
    if (type == "graphic_eq") return std::make_shared<fa::GraphicEq>();
    if (type == "dc_blocker") return std::make_shared<fa::DcBlocker>();
    if (type == "compressor") return std::make_shared<fa::Compressor>();
    if (type == "limiter") return std::make_shared<fa::Limiter>();
    if (type == "noise_gate" || type == "gate") return std::make_shared<fa::NoiseGate>();
    if (type == "expander") return std::make_shared<fa::Expander>();
    if (type == "de_esser") return std::make_shared<fa::DeEsser>();
    if (type == "delay" || type == "echo") return std::make_shared<fa::Delay>();
    if (type == "reverb") return std::make_shared<fa::Reverb>();
    if (type == "distortion") return std::make_shared<fa::Distortion>();
    if (type == "bitcrusher") return std::make_shared<fa::Bitcrusher>();
    if (type == "chorus") return std::make_shared<fa::Chorus>();
    if (type == "flanger") return std::make_shared<fa::Flanger>();
    if (type == "phaser") return std::make_shared<fa::Phaser>();
    if (type == "tremolo") return std::make_shared<fa::Tremolo>();
    if (type == "vibrato") return std::make_shared<fa::Vibrato>();
    if (type == "autopan" || type == "auto_pan") return std::make_shared<fa::AutoPan>();
    if (type == "noise_reduction") return std::make_shared<fa::NoiseReduction>();
    if (type == "hum_remover") return std::make_shared<fa::HumRemover>();
    if (type == "click_remover") return std::make_shared<fa::ClickRemover>();
    if (type == "pitch_shifter" || type == "pitch") return std::make_shared<fa::PitchShifter>();
    if (type == "formant_shifter") return std::make_shared<fa::FormantShifter>();
    if (type == "autotune") return std::make_shared<fa::AutoTune>();
    if (type == "voice_eq") return std::make_shared<fa::VoiceEQ>();
    if (type == "ring_modulator") return std::make_shared<fa::RingModulator>();
    if (type == "vocoder") return std::make_shared<fa::Vocoder>();
    if (type == "voice_changer") return std::make_shared<fa::VoiceChanger>();
    for (std::size_t i = 1; i < fa::voice_preset_count(); ++i) {
        const fa::VoicePreset preset = static_cast<fa::VoicePreset>(i);
        if (type == fa::voice_preset_name(preset)) return std::make_shared<fa::VoiceChanger>(preset);
    }
    return nullptr;
}

std::shared_ptr<fa::Effect> effect_locked(Engine& e, std::int64_t handle) {
    auto it = e.effects.find(handle);
    return it == e.effects.end() ? nullptr : it->second.effect;
}

void forget_finished(Engine& e) {
    for (auto it = e.playing.begin(); it != e.playing.end();) {
        if (!it->second.pending && !e.mixer().is_playing(it->second.voice)) it = e.playing.erase(it);
        else ++it;
    }
}

constexpr const char* k_record_audio = "android.permission.RECORD_AUDIO";

std::atomic<bool>& monitors_waiting() {
    static std::atomic<bool> waiting{ false };
    return waiting;
}

bool microphone_permission_pending() {
    return fa::android_permission_state(k_record_audio) == fa::AndroidPermissionState::pending;
}

bool open_monitor(Monitor& m, std::string& error) {
    Monitor* raw = &m;
    const std::uint32_t rate = mixer_rate();
    fa::StreamConfig in;
    in.kind = fa::DeviceKind::input;
    in.device = m.input_device;
    in.sample_rate = rate;
    in.latency_ms = 10;
    in.name = "grove monitor";
    std::string why;
    if (!raw->capture.open_capture(in, [raw](const float* data, std::size_t frames) { raw->on_capture(data, frames); }, &why)) {
        error = why.empty() ? std::string("can't open the microphone") : why;
        raw->capture.close();
        return false;
    }
    fa::StreamConfig out;
    out.kind = fa::DeviceKind::output;
    out.device = m.output_device;
    out.sample_rate = rate;
    out.channels = 2;
    out.latency_ms = 10;
    out.name = "grove monitor";
    if (!raw->playback.open_playback(out, [raw](float* data, std::size_t frames) { raw->on_playback(data, frames); }, &why)) {
        error = why.empty() ? std::string("can't open the audio output") : why;
        raw->capture.close();
        raw->playback.close();
        return false;
    }
    raw->in_channels = std::max<std::uint32_t>(1, raw->capture.channels());
    raw->out_channels = std::max<std::uint32_t>(1, raw->playback.channels());
    raw->scratch.assign(4096 * raw->out_channels, 0.0f);
    raw->ring.reset(static_cast<std::size_t>(rate) * raw->out_channels / 2);
    raw->max_backlog = static_cast<std::size_t>(rate) * raw->out_channels * 60 / 1000;
    raw->chain->prepare(rate, raw->out_channels);
    if (!raw->playback.start(&why) || !raw->capture.start(&why)) {
        error = why.empty() ? std::string("can't start the microphone") : why;
        raw->capture.close();
        raw->playback.close();
        return false;
    }
    return true;
}

std::shared_ptr<Clip> new_clip(const std::string& path) {
    auto clip = std::make_shared<Clip>();
    clip->path = path;
    return clip;
}

} // namespace

bool supported() noexcept {
    try {
        return !fa::backend_name().empty();
    } catch (...) {
        return false;
    }
}

std::string backend() {
    return fa::backend_name();
}

std::vector<Device> output_devices() {
    return list(fa::DeviceKind::output);
}

std::vector<Device> input_devices() {
    return list(fa::DeviceKind::input);
}

bool set_output_device(const std::string& id, std::string* error) {
    return engine().set_device(id, error);
}

std::string output_device() {
    return engine().device();
}

bool open_output(std::string* error) {
    return engine().open_output(error);
}

bool output_open() noexcept {
    if (!engine_exists()) return false;
    Engine& e = engine();
    return e.manual() || e.mixer().output_running();
}

std::string last_error() {
    return engine().error();
}

void set_master_volume(double volume) {
    if (!std::isfinite(volume)) return;
    engine().mixer().set_master_gain(static_cast<float>(std::max(0.0, std::min(4.0, volume))));
}

double master_volume() {
    return engine().mixer().master_gain();
}

double master_level() {
    if (!engine_exists()) return 0.0;
    return engine().mixer().output_peak();
}

void set_manual_output(bool manual) {
    g_manual = manual;
    engine().set_manual(manual);
}

bool manual_output() noexcept {
    return g_manual;
}

void advance(double seconds) {
    engine().advance(seconds);
}

void shutdown() {
    std::shared_ptr<Engine> e;
    {
        std::lock_guard<std::mutex> lock(engine_mutex());
        e = std::move(engine_slot());
    }
    if (e) e->stop();
}

std::uint32_t mixer_rate() noexcept {
    return 48000;
}

std::shared_ptr<fizmo::audio::Mixer> shared_mixer() {
    engine();
    std::lock_guard<std::mutex> lock(engine_mutex());
    std::shared_ptr<Engine>& slot = engine_slot();
    if (!slot) return nullptr;
    return std::shared_ptr<fizmo::audio::Mixer>(slot, &slot->mixer());
}

std::shared_ptr<Clip> load_clip(const std::string& path, bool decode) {
    return load_clip(path, decode, false);
}

std::shared_ptr<Clip> load_clip(const std::string& path, bool decode, bool allow_stream) {
    std::shared_ptr<Clip> clip = new_clip(path);
    clip->allow_stream = allow_stream;
    clip->queued = true;
    engine().enqueue(clip, decode);
    return clip;
}

void decode_clip(const std::shared_ptr<Clip>& clip) {
    if (!clip) return;
    {
        std::lock_guard<std::mutex> lock(clip->mutex);
        if (clip->done || clip->queued) return;
        clip->queued = true;
    }
    engine().enqueue(clip, true);
}

void cancel_clip(const std::shared_ptr<Clip>& clip) {
    if (clip) clip->cancel.store(true);
}

ClipStatus clip_status(const Clip& clip) {
    std::lock_guard<std::mutex> lock(clip.mutex);
    ClipStatus s;
    s.done = clip.done;
    s.failed = clip.failed;
    s.has_info = clip.has_info;
    s.duration = clip.duration;
    s.error = clip.error;
    return s;
}

std::shared_ptr<const fizmo::audio::AudioBuffer> clip_buffer(const Clip& clip) {
    std::lock_guard<std::mutex> lock(clip.mutex);
    return clip.sound;
}

bool clip_streams(const Clip& clip) {
    std::lock_guard<std::mutex> lock(clip.mutex);
    return clip.stream;
}

std::uint64_t start_voice(const std::shared_ptr<Clip>& clip, const VoiceOptions& options, std::string* error) {
    if (!clip) return 0;
    Engine& e = engine();
    const bool streamed = clip_streams(*clip);
    const fa::Sound sound = streamed ? fa::Sound() : sound_for(*clip, options.clip_start, options.clip_end);
    if (!streamed && !sound) {
        if (error) *error = "there's nothing to play";
        return 0;
    }
    if (!e.open_output(error)) return 0;
    fa::PlayOptions o;
    o.gain = static_cast<float>(options.gain);
    o.pan = static_cast<float>(options.pan);
    o.speed = static_cast<float>(options.speed > 0.0 ? options.speed : 1.0);
    o.loop = options.loop;
    o.paused = options.paused;
    o.start = std::max(0.0, options.start);
    o.preserve_pitch = options.preserve_pitch;
    if (!streamed) return e.mixer().play(sound, o);
    std::string why;
    const std::uint64_t voice = e.mixer().play_file(clip->path, o, &why);
    if (!voice && error) *error = why.empty() ? std::string("can't play '" + clip->path + "'") : why;
    return voice;
}

void stop_voice(std::uint64_t voice, double fade) {
    if (voice) engine().mixer().stop(voice, fade);
}

void pause_voice(std::uint64_t voice, bool paused) {
    if (voice) engine().mixer().set_paused(voice, paused);
}

bool seek_voice(std::uint64_t voice, double seconds) {
    return voice && engine().mixer().seek(voice, std::max(0.0, seconds));
}

void set_voice_gain(std::uint64_t voice, double gain) {
    if (voice) engine().mixer().set_gain(voice, static_cast<float>(gain));
}

void set_voice_pan(std::uint64_t voice, double pan) {
    if (voice) engine().mixer().set_pan(voice, static_cast<float>(pan));
}

void set_voice_speed(std::uint64_t voice, double speed) {
    if (voice) engine().mixer().set_speed(voice, static_cast<float>(speed));
}

void set_voice_loop(std::uint64_t voice, bool loop) {
    if (voice) engine().mixer().set_loop(voice, loop);
}

bool voice_active(std::uint64_t voice) {
    return voice && engine().mixer().is_playing(voice);
}

double voice_position(std::uint64_t voice) {
    return voice ? engine().mixer().position(voice) : 0.0;
}

std::int64_t preload_sound(const std::string& path) {
    if (path.empty()) return 0;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto found = e.sound_paths.find(path);
    if (found != e.sound_paths.end() && e.sounds.count(found->second)) return found->second;
    std::shared_ptr<Clip> clip = new_clip(path);
    clip->queued = true;
    const std::int64_t id = e.next_id();
    e.sounds[id] = SoundEntry{ clip, path };
    e.sound_paths[path] = id;
    e.enqueue(clip, true);
    return id;
}

bool sound_loaded(std::int64_t sound) {
    Engine& e = engine();
    std::shared_ptr<Clip> clip;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        auto it = e.sounds.find(sound);
        if (it == e.sounds.end()) return false;
        clip = it->second.clip;
    }
    const ClipStatus s = clip_status(*clip);
    return s.done && !s.failed;
}

std::string sound_error(std::int64_t sound) {
    Engine& e = engine();
    std::shared_ptr<Clip> clip;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        auto it = e.sounds.find(sound);
        if (it == e.sounds.end()) return "there's no sound " + std::to_string(sound);
        clip = it->second.clip;
    }
    return clip_status(*clip).error;
}

double sound_duration(std::int64_t sound) {
    Engine& e = engine();
    std::shared_ptr<Clip> clip;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        auto it = e.sounds.find(sound);
        if (it == e.sounds.end()) return 0.0;
        clip = it->second.clip;
    }
    return clip_status(*clip).duration;
}

bool unload_sound(std::int64_t sound) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.sounds.find(sound);
    if (it == e.sounds.end()) return false;
    auto p = e.sound_paths.find(it->second.path);
    if (p != e.sound_paths.end() && p->second == sound) e.sound_paths.erase(p);
    e.sounds.erase(it);
    return true;
}

std::int64_t play_sound(std::int64_t sound, double volume, double pan, bool loop) {
    Engine& e = engine();
    std::shared_ptr<Clip> clip;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        auto it = e.sounds.find(sound);
        if (it == e.sounds.end()) return 0;
        clip = it->second.clip;
    }
    if (!std::isfinite(volume)) volume = 1.0;
    if (!std::isfinite(pan)) pan = 0.0;
    volume = std::max(0.0, volume);
    pan = std::max(-1.0, std::min(1.0, pan));
    std::lock_guard<std::mutex> lock(e.mutex());
    forget_finished(e);
    const std::int64_t id = e.next_id();
    PlayingSound p;
    p.clip = clip;
    p.volume = volume;
    p.pan = pan;
    p.loop = loop;
    fa::Sound ready;
    {
        std::lock_guard<std::mutex> clock(clip->mutex);
        if (clip->failed) {
            e.set_error_unlocked(clip->error);
            return 0;
        }
        if (clip->done) ready = clip->sound;
        else clip->waiting.push_back(id);
    }
    if (ready) {
        std::string why;
        if (!e.open_output_locked(&why)) return 0;
        fa::PlayOptions o;
        o.gain = static_cast<float>(volume);
        o.pan = static_cast<float>(pan);
        o.loop = loop;
        p.voice = e.mixer().play(ready, o);
        p.pending = false;
        if (!p.voice) return 0;
    }
    e.playing[id] = p;
    return id;
}

std::int64_t play_sound(const std::string& path, double volume, double pan, bool loop) {
    const std::int64_t sound = preload_sound(path);
    return sound ? play_sound(sound, volume, pan, loop) : 0;
}

bool stop_sound(std::int64_t id, double fade) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.playing.find(id);
    if (it == e.playing.end()) return false;
    if (it->second.voice) e.mixer().stop(it->second.voice, std::max(0.0, fade));
    e.playing.erase(it);
    return true;
}

bool set_sound_volume(std::int64_t id, double volume) {
    if (!std::isfinite(volume)) return false;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.playing.find(id);
    if (it == e.playing.end()) return false;
    it->second.volume = std::max(0.0, volume);
    if (it->second.voice) e.mixer().set_gain(it->second.voice, static_cast<float>(it->second.volume));
    return true;
}

bool set_sound_pan(std::int64_t id, double pan) {
    if (!std::isfinite(pan)) return false;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.playing.find(id);
    if (it == e.playing.end()) return false;
    it->second.pan = std::max(-1.0, std::min(1.0, pan));
    if (it->second.voice) e.mixer().set_pan(it->second.voice, static_cast<float>(it->second.pan));
    return true;
}

bool sound_playing(std::int64_t id) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.playing.find(id);
    if (it == e.playing.end()) return false;
    if (it->second.pending) return true;
    return e.mixer().is_playing(it->second.voice);
}

void stop_all_sounds(double fade) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    for (auto& kv : e.playing) {
        if (kv.second.voice) e.mixer().stop(kv.second.voice, std::max(0.0, fade));
    }
    e.playing.clear();
}

void update_sounds() {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    forget_finished(e);
}

std::vector<std::string> effect_types() {
    std::vector<std::string> out = {
        "chain", "gain", "filter", "lowpass", "highpass", "bandpass", "notch", "parametric_eq", "graphic_eq", "dc_blocker",
        "compressor", "limiter", "noise_gate", "expander", "de_esser", "delay", "reverb", "distortion", "bitcrusher",
        "chorus", "flanger", "phaser", "tremolo", "vibrato", "autopan", "noise_reduction", "hum_remover", "click_remover",
        "pitch_shifter", "formant_shifter", "autotune", "voice_eq", "ring_modulator", "vocoder", "voice_changer"
    };
    for (const fa::EffectTypeInfo& info : fa::effect_types()) {
        if (std::find(out.begin(), out.end(), info.type) == out.end()) out.push_back(info.type);
    }
    for (std::size_t i = 1; i < fa::voice_preset_count(); ++i) out.push_back(fa::voice_preset_name(static_cast<fa::VoicePreset>(i)));
    return out;
}

std::int64_t create_effect(const std::string& type) {
    std::shared_ptr<fa::Effect> made = make_effect(type);
    if (!made) return 0;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    const std::int64_t id = e.next_id();
    e.effects[id] = EffectEntry{ made, made->name() };
    return id;
}

std::shared_ptr<fizmo::audio::Effect> effect(std::int64_t handle) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    return effect_locked(e, handle);
}

std::int64_t add_effect(std::shared_ptr<fizmo::audio::Effect> made) {
    if (!made) return 0;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    const std::int64_t id = e.next_id();
    const std::string type = made->name();
    e.effects[id] = EffectEntry{ std::move(made), type };
    return id;
}

bool free_effect(std::int64_t handle) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    return e.effects.erase(handle) != 0;
}

std::string effect_type(std::int64_t handle) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.effects.find(handle);
    return it == e.effects.end() ? std::string() : it->second.type;
}

bool set_effect_param(std::int64_t handle, const std::string& name, double value) {
    const std::shared_ptr<fa::Effect> fx = effect(handle);
    return fx && std::isfinite(value) && fx->set(name, static_cast<float>(value));
}

double effect_param(std::int64_t handle, const std::string& name) {
    const std::shared_ptr<fa::Effect> fx = effect(handle);
    if (!fx) return 0.0;
    const fa::Parameter* p = fx->parameter(name);
    return p ? static_cast<double>(p->get()) : 0.0;
}

std::vector<std::string> effect_params(std::int64_t handle) {
    std::vector<std::string> out;
    const std::shared_ptr<fa::Effect> fx = effect(handle);
    if (!fx) return out;
    for (const fa::Parameter* p : fx->parameters()) out.push_back(p->name());
    return out;
}

bool set_effect_enabled(std::int64_t handle, bool enabled) {
    const std::shared_ptr<fa::Effect> fx = effect(handle);
    if (!fx) return false;
    fx->set_enabled(enabled);
    return true;
}

bool chain_add(std::int64_t chain, std::int64_t added) {
    if (chain == added) return false;
    const std::shared_ptr<fa::EffectChain> c = std::dynamic_pointer_cast<fa::EffectChain>(effect(chain));
    const std::shared_ptr<fa::Effect> fx = effect(added);
    if (!c || !fx) return false;
    c->add(fx);
    return true;
}

bool chain_remove(std::int64_t chain, std::size_t index) {
    const std::shared_ptr<fa::EffectChain> c = std::dynamic_pointer_cast<fa::EffectChain>(effect(chain));
    return c && c->remove(index);
}

bool chain_clear(std::int64_t chain) {
    const std::shared_ptr<fa::EffectChain> c = std::dynamic_pointer_cast<fa::EffectChain>(effect(chain));
    if (!c) return false;
    c->clear();
    return true;
}

std::size_t chain_size(std::int64_t chain) {
    const std::shared_ptr<fa::EffectChain> c = std::dynamic_pointer_cast<fa::EffectChain>(effect(chain));
    return c ? c->size() : 0;
}

std::int64_t start_monitor(const std::string& input_device, const std::string& output_device, std::int64_t effect_handle, std::string* error) {
    Engine& e = engine();
    std::shared_ptr<fa::Effect> fx;
    if (effect_handle) {
        fx = effect(effect_handle);
        if (!fx) {
            if (error) *error = "there's no effect " + std::to_string(effect_handle);
            return 0;
        }
    }
    auto m = std::make_unique<Monitor>();
    m->input_device = input_device;
    m->output_device = output_device;
    if (fx) m->chain->add(fx);
    m->effect = effect_handle;
    std::string why;
    if (!open_monitor(*m, why)) {
        if (error) *error = why;
        e.set_error(why);
        if (!microphone_permission_pending()) return 0;
        m->waiting = true;
        m->error = why;
        std::lock_guard<std::mutex> lock(e.mutex());
        const std::int64_t id = e.next_id();
        e.monitors()[id] = std::move(m);
        monitors_waiting().store(true);
        return id;
    }
    std::lock_guard<std::mutex> lock(e.mutex());
    const std::int64_t id = e.next_id();
    e.monitors()[id] = std::move(m);
    return id;
}

bool monitor_waiting(std::int64_t monitor) {
    if (!engine_exists()) return false;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    return it != e.monitors().end() && it->second->waiting;
}

std::string monitor_error(std::int64_t monitor) {
    if (!engine_exists()) return std::string();
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    if (it == e.monitors().end()) return std::string();
    return it->second->waiting || it->second->failed ? it->second->error : std::string();
}

std::vector<std::int64_t> update_monitors() {
    std::vector<std::int64_t> failed;
    if (!monitors_waiting().load() || !engine_exists()) return failed;
    using Clock = std::chrono::steady_clock;
    static std::mutex poll_mutex;
    static Clock::time_point last_poll;
    {
        std::lock_guard<std::mutex> lock(poll_mutex);
        const Clock::time_point now = Clock::now();
        if (last_poll != Clock::time_point{} && now - last_poll < std::chrono::milliseconds(250)) return failed;
        last_poll = now;
    }
    const fa::AndroidPermissionState state = fa::android_permission_state(k_record_audio);
    if (state == fa::AndroidPermissionState::pending) return failed;
    Engine& e = engine();
    std::vector<std::pair<std::int64_t, std::unique_ptr<Monitor>>> work;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        for (auto it = e.monitors().begin(); it != e.monitors().end();) {
            if (it->second->waiting) {
                work.emplace_back(it->first, std::move(it->second));
                it = e.monitors().erase(it);
            } else {
                ++it;
            }
        }
        monitors_waiting().store(false);
    }
    for (auto& item : work) {
        Monitor& m = *item.second;
        m.waiting = false;
        std::string why;
        if (state == fa::AndroidPermissionState::granted) {
            if (open_monitor(m, why)) {
                m.error.clear();
                continue;
            }
        } else {
            why = "the microphone wasn't allowed: the user didn't give the app Android's RECORD_AUDIO permission";
        }
        m.failed = true;
        m.error = why;
        e.set_error(why);
        failed.push_back(item.first);
    }
    std::lock_guard<std::mutex> lock(e.mutex());
    for (auto& item : work) e.monitors()[item.first] = std::move(item.second);
    return failed;
}

bool stop_monitor(std::int64_t monitor) {
    Engine& e = engine();
    std::unique_ptr<Monitor> m;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        auto it = e.monitors().find(monitor);
        if (it == e.monitors().end()) return false;
        m = std::move(it->second);
        e.monitors().erase(it);
    }
    m->capture.close();
    m->playback.close();
    return true;
}

bool monitor_running(std::int64_t monitor) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    return it != e.monitors().end() && it->second->capture.running() && it->second->playback.running();
}

bool set_monitor_effect(std::int64_t monitor, std::int64_t effect_handle) {
    std::shared_ptr<fa::Effect> fx;
    if (effect_handle) {
        fx = effect(effect_handle);
        if (!fx) return false;
    }
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    if (it == e.monitors().end()) return false;
    it->second->chain->clear();
    if (fx) it->second->chain->add(fx);
    it->second->effect = effect_handle;
    return true;
}

bool set_monitor_volume(std::int64_t monitor, double volume) {
    if (!std::isfinite(volume)) return false;
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    if (it == e.monitors().end()) return false;
    it->second->volume.store(static_cast<float>(std::max(0.0, std::min(4.0, volume))));
    return true;
}

double monitor_input_level(std::int64_t monitor) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    return it == e.monitors().end() ? 0.0 : static_cast<double>(it->second->in_level.load());
}

double monitor_output_level(std::int64_t monitor) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex());
    auto it = e.monitors().find(monitor);
    return it == e.monitors().end() ? 0.0 : static_cast<double>(it->second->out_level.load());
}

void stop_all_monitors() {
    if (!engine_exists()) return;
    Engine& e = engine();
    std::map<std::int64_t, std::unique_ptr<Monitor>> all;
    {
        std::lock_guard<std::mutex> lock(e.mutex());
        all.swap(e.monitors());
    }
    for (auto& kv : all) {
        kv.second->capture.close();
        kv.second->playback.close();
    }
}

double level_to_db(double level) {
    return level > 1e-6 ? 20.0 * std::log10(level) : -120.0;
}

} // namespace audio
} // namespace grove
