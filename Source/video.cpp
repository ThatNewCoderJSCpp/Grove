#include "grove/media.hpp"
#include "grove/audio.hpp"
#include "grove/project.hpp"

#include <fizmo/../../Source/Audio/audio.hpp>
#include <fizmo/../../Source/Video/player.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace grove {

namespace {

namespace fv = fizmo::video;

using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

std::atomic<bool>& planar_preference() {
    static std::atomic<bool> enabled{ false };
    return enabled;
}

Bytes shared_bytes(const std::string& path) {
    static std::mutex* mutex = new std::mutex();
    static std::map<std::string, std::weak_ptr<const std::vector<std::uint8_t>>>* cache = new std::map<std::string, std::weak_ptr<const std::vector<std::uint8_t>>>();
    std::lock_guard<std::mutex> lock(*mutex);
    const std::string key = virtual_file_key(path);
    auto it = cache->find(key);
    if (it != cache->end()) {
        if (Bytes kept = it->second.lock()) return kept;
    }
    const std::optional<std::string> bytes = read_file(path);
    if (!bytes || bytes->empty()) return nullptr;
    Bytes made = std::make_shared<const std::vector<std::uint8_t>>(bytes->begin(), bytes->end());
    (*cache)[key] = made;
    return made;
}

bool audio_allowed() {
    return audio::supported();
}

fv::PlayerOptions player_options() {
    fv::PlayerOptions o;
    o.rgba = true;
    o.enable_audio = audio_allowed();
    return o;
}

class VideoPlayer : public MediaPlayer {
public:
    VideoPlayer() : m_player(player_options()) {}

    ~VideoPlayer() override {
        m_player.close();
        m_player.detach();
        m_mixer.reset();
    }

    void open(const std::string& path, bool full) override {
        (void)full;
        close();
        m_path = path;
        fv::PlayerOptions o = m_player.options();
        const bool rgba = !planar_preference().load();
        if (o.rgba != rgba) {
            o.rgba = rgba;
            m_player.set_options(o);
        }
        attach_mixer();
        std::string why;
        bool ok = false;
        if (is_virtual_file(path)) {
            const Bytes bytes = shared_bytes(path);
            if (bytes) ok = m_player.open_shared(bytes, &why);
            else why = "can't read it";
        } else if (!path_exists(path)) {
            why = "there's no such file";
        } else {
            ok = m_player.open(path, &why);
        }
        if (!ok) {
            m_failed = true;
            m_error = "can't play '" + path + "': " + (why.empty() ? std::string("not a video file Grove can read") : why);
            return;
        }
        m_open = true;
        m_serial = 0;
        apply_loop();
        if (m_start > 0.0) m_player.seek(m_start, true);
        m_pos = m_start;
    }

    void load_fully() override {}

    void close() override {
        m_player.close();
        m_open = false;
        m_failed = false;
        m_running = false;
        m_error.clear();
        m_serial = 0;
        m_pos = m_start;
        m_player.poll_events();
    }

    MediaLoad load_state() const override {
        if (m_failed) return MediaLoad::Failed;
        return m_open ? MediaLoad::Ready : MediaLoad::Idle;
    }

    std::string error() const override {
        if (!m_error.empty()) return m_error;
        return m_open ? m_player.error() : std::string();
    }

    double duration() const override { return m_open ? m_player.duration() : 0.0; }

    bool start(double position, std::string& error) override {
        if (!m_open) {
            error = "nothing is loaded";
            return false;
        }
        position = clamp(position);
        if (m_player.has_audio() && m_mixer) {
            std::string why;
            if (!audio::open_output(&why)) {
                m_player.detach();
                m_mixer.reset();
                m_events.push_back(MediaPlayerEvent{ "warning", "no audio output (" + (why.empty() ? std::string("can't open it") : why) + "); playing without sound" });
            }
        }
        if (m_player.ended() || std::fabs(position - m_player.position()) > 1e-3) m_player.seek(position, true);
        m_player.play();
        m_running = true;
        m_pos = position;
        return true;
    }

    void pause() override {
        if (!m_open) return;
        m_pos = position();
        m_player.pause();
        m_running = false;
    }

    void seek(double position) override {
        position = clamp(position);
        const double d = duration();
        if (d > 0.0 && position > d - 0.001) position = std::max(m_start, d - 0.001);
        m_pos = position;
        if (m_open) m_player.seek(position, true);
    }

    double position() const override {
        if (!m_open) return m_pos;
        const double p = m_player.position();
        const double e = end_time();
        return e > 0.0 ? std::min(std::max(m_start, p), e) : std::max(m_start, p);
    }

    bool finished() override {
        if (!m_open || !m_running) return false;
        const bool at_end = m_player.ended() || (m_end >= 0.0 && !m_player.is_seeking() && m_player.position() >= m_end);
        if (!at_end) return false;
        if (m_loop) {
            m_player.seek(m_start, true);
            m_player.play();
            return false;
        }
        m_player.pause();
        m_running = false;
        m_pos = end_time();
        return true;
    }

    void set_gain(double gain) override { m_player.set_volume(static_cast<float>(std::max(0.0, gain))); }

    void set_rate(double rate) override { m_player.set_rate(std::max(0.25, std::min(4.0, rate))); }

    void set_loop(bool loop) override {
        m_loop = loop;
        apply_loop();
    }

    void set_range(double start, double end) override {
        m_start = std::max(0.0, start);
        m_end = end;
        apply_loop();
        if (!m_open) {
            m_pos = m_start;
            return;
        }
        const double p = m_player.position();
        if (p < m_start || (m_end >= 0.0 && p > m_end)) seek(p);
    }

    double level() const override {
        if (!m_open || !m_running || !m_player.has_audio()) return 0.0;
        return audio::master_level();
    }

    void set_preserve_pitch(bool preserve) override { m_player.set_preserve_pitch(preserve); }

    bool has_video() const override { return true; }

    bool video_size(double& width, double& height) const override {
        if (!m_open) return false;
        const fv::MediaInfo& info = m_player.info();
        if (info.video_track < 0) return false;
        double w = info.display_width ? info.display_width : info.width;
        double h = info.display_height ? info.display_height : info.height;
        if (w <= 0.0 || h <= 0.0) return false;
        if (info.rotation == 90 || info.rotation == 270) std::swap(w, h);
        width = w;
        height = h;
        return true;
    }

    bool frame(MediaFrame& out) override {
        out = MediaFrame{};
        if (!m_open) return false;
        const std::uint8_t* rgba = nullptr;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        m_player.current_rgba(rgba, w, h, m_serial);
        out.serial = m_serial;
        if (!rgba || w == 0 || h == 0) return false;
        const fv::MediaInfo& info = m_player.info();
        out.rgba = rgba;
        out.width = w;
        out.height = h;
        out.rotation = info.rotation;
        out.aspect = info.display_aspect > 0.0 ? info.display_aspect : static_cast<double>(w) / static_cast<double>(h);
        return true;
    }

    bool planar_frame(MediaFrame& out) override {
        out = MediaFrame{};
        if (!m_open) return false;
        const fv::VideoFrame* f = nullptr;
        m_player.current_frame(f, m_serial);
        out.serial = m_serial;
        if (!f || f->empty() || f->width == 0 || f->height == 0) return false;
        const fv::MediaInfo& info = m_player.info();
        out.planes = f;
        out.width = f->width;
        out.height = f->height;
        out.rotation = info.rotation;
        out.aspect = info.display_aspect > 0.0 ? info.display_aspect : static_cast<double>(f->width) / static_cast<double>(f->height);
        return true;
    }

    double frame_delay() const override { return m_open ? m_player.next_frame_delay() : -1.0; }

    bool frame_pending() const override {
        if (!m_open || !m_player.has_video()) return false;
        return m_player.is_seeking() || m_player.frame_serial() == 0 || m_player.next_frame_delay() == 0.0;
    }

    std::vector<MediaPlayerEvent> take_events() override {
        std::vector<MediaPlayerEvent> out;
        out.swap(m_events);
        if (!m_open) return out;
        for (const fv::PlayerEvent& e : m_player.poll_events()) {
            switch (e.type) {
                case fv::PlayerEventType::first_frame: out.push_back(MediaPlayerEvent{ "loadeddata", std::string() }); break;
                case fv::PlayerEventType::waiting: out.push_back(MediaPlayerEvent{ "waiting", std::string() }); break;
                case fv::PlayerEventType::playing: out.push_back(MediaPlayerEvent{ "playing", std::string() }); break;
                case fv::PlayerEventType::warning: out.push_back(MediaPlayerEvent{ "warning", e.message }); break;
                case fv::PlayerEventType::error:
                    m_error = e.message;
                    out.push_back(MediaPlayerEvent{ "error", e.message });
                    break;
                default: break;
            }
        }
        return out;
    }

private:
    void attach_mixer() {
        if (!audio_allowed()) return;
        if (!m_mixer) {
            m_mixer = audio::shared_mixer();
            if (m_mixer) m_player.attach(*m_mixer);
        }
    }

    void apply_loop() { m_player.set_loop(m_loop && m_start <= 0.0 && m_end < 0.0); }

    double end_time() const {
        const double d = duration();
        if (m_end >= 0.0 && (d <= 0.0 || m_end < d)) return m_end;
        return d;
    }

    double clamp(double t) const {
        if (!std::isfinite(t)) t = m_start;
        t = std::max(m_start, t);
        const double e = end_time();
        if (e > 0.0) t = std::min(t, e);
        return t;
    }

    fv::MediaPlayer m_player;
    std::shared_ptr<fizmo::audio::Mixer> m_mixer;
    std::string m_path;
    std::string m_error;
    std::vector<MediaPlayerEvent> m_events;
    std::uint64_t m_serial = 0;
    bool m_open = false;
    bool m_failed = false;
    bool m_running = false;
    bool m_loop = false;
    double m_start = 0.0;
    double m_end = -1.0;
    double m_pos = 0.0;
};

} // namespace

std::unique_ptr<MediaPlayer> make_video_player() {
    return std::make_unique<VideoPlayer>();
}

void set_media_planar_frames(bool enabled) noexcept {
    planar_preference().store(enabled);
}

bool media_planar_frames() noexcept {
    return planar_preference().load();
}

void media_frame_size(const MediaFrame& frame, unsigned& width, unsigned& height) {
    const bool turned = frame.rotation == 90 || frame.rotation == 270;
    width = turned ? frame.height : frame.width;
    height = turned ? frame.width : frame.height;
}

void copy_media_frame(const MediaFrame& frame, std::uint8_t* rgba) {
    if (!frame.rgba || !rgba) return;
    const std::size_t w = frame.width;
    const std::size_t h = frame.height;
    const std::uint8_t* in = frame.rgba;
    if (frame.rotation != 90 && frame.rotation != 180 && frame.rotation != 270) {
        std::copy(in, in + w * h * 4, rgba);
        return;
    }
    if (frame.rotation == 180) {
        for (std::size_t y = 0; y < h; ++y) {
            const std::uint8_t* row = in + (h - 1 - y) * w * 4;
            std::uint8_t* out = rgba + y * w * 4;
            for (std::size_t x = 0; x < w; ++x) std::copy(row + (w - 1 - x) * 4, row + (w - x) * 4, out + x * 4);
        }
        return;
    }
    const std::size_t ow = h;
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* row = in + y * w * 4;
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t ox = frame.rotation == 90 ? h - 1 - y : y;
            const std::size_t oy = frame.rotation == 90 ? x : w - 1 - x;
            std::copy(row + x * 4, row + x * 4 + 4, rgba + (oy * ow + ox) * 4);
        }
    }
}

} // namespace grove
