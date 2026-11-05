#include "grove/media.hpp"
#include "grove/audio.hpp"
#include "grove/canvas.hpp"
#include "grove/input/input.hpp"
#include "grove/project.hpp"

#include <fizmo/../../Source/Audio/audio.hpp>
#include <fizmo/../../Source/Video/player.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace grove {

struct MediaState {
    Node* node = nullptr;
    std::unique_ptr<MediaPlayer> player;
    std::string src;
    std::string error;
    bool playing = false;
    bool want_start = false;
    bool meta_sent = false;
    bool ready_sent = false;
    bool error_sent = false;
    bool scrubbing = false;
    double duration = 0.0;
    double volume = 1.0;
    double rate = 1.0;
    bool muted = false;
    bool loop = false;
    bool preserve = true;
    double range_start = 0.0;
    double range_end = -1.0;
    double last_update = -std::numeric_limits<double>::infinity();
    std::uint64_t frame_timer = 0;
    std::uint64_t frame_serial = 0;
    double video_width = 0.0;
    double video_height = 0.0;
    bool shown = false;
    bool decoding = false;
    double controls_until = 0.0;
};

struct MeterState {
    double shown = 0.0;
};

struct MediaRegistry {
    std::map<const Node*, std::unique_ptr<MediaState>> items;
    std::map<Node*, MeterState> meters;
    std::vector<std::pair<Node*, Event>> queue;
    std::uint64_t poll_timer = 0;
    std::uint64_t flush_timer = 0;
    bool started = false;
};

namespace {

class AudioPlayer : public MediaPlayer {
public:
    ~AudioPlayer() override { close(); }

    void open(const std::string& path, bool full) override {
        close();
        m_path = path;
        m_clip = audio::load_clip(path, full, true);
    }

    void load_fully() override {
        if (!m_clip) {
            if (!m_path.empty()) m_clip = audio::load_clip(m_path, true, true);
            return;
        }
        audio::decode_clip(m_clip);
    }

    void close() override {
        if (m_voice) audio::stop_voice(m_voice);
        m_voice = 0;
        m_running = false;
        if (m_clip) audio::cancel_clip(m_clip);
        m_clip.reset();
        m_pos = m_start;
        m_seek_target = -1.0;
    }

    MediaLoad load_state() const override {
        if (!m_clip) return MediaLoad::Idle;
        const audio::ClipStatus s = audio::clip_status(*m_clip);
        if (s.failed) return MediaLoad::Failed;
        if (s.done) return MediaLoad::Ready;
        if (s.has_info) return MediaLoad::Metadata;
        return MediaLoad::Loading;
    }

    std::string error() const override { return m_clip ? audio::clip_status(*m_clip).error : std::string(); }

    double duration() const override { return m_clip ? audio::clip_status(*m_clip).duration : 0.0; }

    bool start(double position, std::string& error) override {
        if (!m_clip) {
            error = "nothing is loaded";
            return false;
        }
        position = clamp(position);
        if (m_voice && !m_voice_stale && audio::voice_active(m_voice)) {
            if (std::fabs(position - current()) > 1e-6) {
                audio::seek_voice(m_voice, position - offset());
                m_seek_target = position;
                m_seek_from = audio::voice_position(m_voice);
                m_seek_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            }
            audio::pause_voice(m_voice, false);
            m_running = true;
            m_pos = position;
            return true;
        }
        if (m_voice) audio::stop_voice(m_voice);
        m_voice = 0;
        m_streamed = audio::clip_streams(*m_clip);
        audio::VoiceOptions o;
        o.gain = m_gain;
        o.speed = m_rate;
        o.loop = m_loop && (!m_streamed || (m_start <= 0.0 && m_end < 0.0));
        o.preserve_pitch = stretched();
        o.clip_start = m_start;
        o.clip_end = m_end;
        o.start = position - (m_streamed ? 0.0 : m_start);
        m_voice = audio::start_voice(m_clip, o, &error);
        if (!m_voice) return false;
        m_voice_stale = false;
        m_voice_stretched = o.preserve_pitch;
        m_voice_loops = o.loop;
        m_running = true;
        m_pos = position;
        m_seek_target = -1.0;
        return true;
    }

    void pause() override {
        if (!m_voice || !m_running) return;
        m_pos = current();
        audio::pause_voice(m_voice, true);
        m_running = false;
        m_seek_target = -1.0;
    }

    void seek(double position) override {
        position = clamp(position);
        m_pos = position;
        if (!m_voice) return;
        audio::seek_voice(m_voice, position - offset());
        if (m_running) {
            m_seek_target = position;
            m_seek_from = audio::voice_position(m_voice);
            m_seek_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        }
    }

    double position() const override { return m_running ? current() : m_pos; }

    bool finished() override {
        if (!m_voice || !m_running) return false;
        const bool active = audio::voice_active(m_voice);
        const bool past_end = active && m_streamed && m_end >= 0.0 && m_seek_target < 0.0 && current() >= m_end;
        if (active && !past_end) return false;
        if (m_loop && !m_voice_loops) {
            std::string ignored;
            if (restart(m_start, ignored)) return false;
        }
        if (active) audio::stop_voice(m_voice);
        m_voice = 0;
        m_running = false;
        m_seek_target = -1.0;
        m_pos = end_time();
        return true;
    }

    void set_gain(double gain) override {
        m_gain = gain;
        audio::set_voice_gain(m_voice, gain);
    }

    void set_rate(double rate) override {
        m_rate = rate;
        if (m_voice && stretched() != m_voice_stretched) {
            replace_voice();
            return;
        }
        audio::set_voice_speed(m_voice, rate);
    }

    void set_loop(bool loop) override {
        m_loop = loop;
        if (!m_voice) return;
        const bool mixer_loops = loop && (!m_streamed || (m_start <= 0.0 && m_end < 0.0));
        m_voice_loops = mixer_loops;
        audio::set_voice_loop(m_voice, mixer_loops);
    }

    void set_preserve_pitch(bool preserve) override {
        m_preserve = preserve;
        if (m_voice && stretched() != m_voice_stretched) replace_voice();
    }

    void set_range(double start, double end) override {
        if (start == m_start && end == m_end) return;
        const double at = position();
        m_start = std::max(0.0, start);
        m_end = end;
        if (m_voice && m_running) {
            std::string ignored;
            restart(clamp(at), ignored);
            return;
        }
        if (m_voice) m_voice_stale = true;
        m_pos = clamp(at);
    }

    double level() const override {
        if (!m_clip || !m_running) return 0.0;
        const std::shared_ptr<const fizmo::audio::AudioBuffer> buffer = audio::clip_buffer(*m_clip);
        if (!buffer || buffer->empty()) return audio::master_level();
        const double at = current();
        const std::size_t frames = buffer->frames();
        const std::size_t centre = std::min(frames, buffer->frame_at(std::max(0.0, at)));
        const std::size_t half = 1024;
        const std::size_t from = centre > half ? centre - half : 0;
        const std::size_t to = std::min(frames, centre + half);
        float peak = 0.0f;
        const std::size_t ch = buffer->channels();
        const float* data = buffer->data();
        for (std::size_t i = from * ch; i < to * ch; ++i) peak = std::max(peak, std::fabs(data[i]));
        return static_cast<double>(peak) * m_gain;
    }

private:
    bool stretched() const { return m_preserve && std::fabs(m_rate - 1.0) > 1e-9; }

    double offset() const { return m_streamed ? 0.0 : m_start; }

    bool restart(double at, std::string& error) {
        if (m_voice) audio::stop_voice(m_voice);
        m_voice = 0;
        m_running = false;
        return start(at, error);
    }

    void replace_voice() {
        if (m_running) {
            std::string ignored;
            restart(current(), ignored);
        } else {
            m_voice_stale = true;
        }
    }

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

    double current() const {
        if (!m_voice) return m_pos;
        if (!audio::voice_active(m_voice)) return end_time();
        const double raw = audio::voice_position(m_voice);
        if (m_seek_target >= 0.0) {
            const bool waiting = m_streamed ? std::fabs(offset() + raw - m_seek_target) > 0.5 && std::chrono::steady_clock::now() < m_seek_deadline : std::fabs(raw - m_seek_from) < 1e-9;
            if (waiting) return m_seek_target;
            m_seek_target = -1.0;
        }
        const double at = offset() + raw;
        const double e = end_time();
        return e > 0.0 ? std::min(at, e) : at;
    }

    std::string m_path;
    std::shared_ptr<audio::Clip> m_clip;
    std::uint64_t m_voice = 0;
    bool m_running = false;
    bool m_voice_stale = false;
    bool m_voice_stretched = false;
    bool m_voice_loops = false;
    bool m_streamed = false;
    bool m_preserve = true;
    double m_pos = 0.0;
    double m_gain = 1.0;
    double m_rate = 1.0;
    bool m_loop = false;
    double m_start = 0.0;
    double m_end = -1.0;
    mutable double m_seek_target = -1.0;
    double m_seek_from = 0.0;
    std::chrono::steady_clock::time_point m_seek_deadline;
};

std::map<std::string, MediaPlayerFactory>& factories() {
    static std::map<std::string, MediaPlayerFactory> list;
    return list;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::unique_ptr<MediaPlayer> make_player(Document& document, Node& node) {
    auto it = factories().find(node.tag());
    if (it != factories().end() && it->second) {
        std::unique_ptr<MediaPlayer> p = it->second(document, node);
        if (p) return p;
    }
    if (node.tag() == "video") return make_video_player();
    return make_audio_player();
}

MediaRegistry& registry(Document& document) {
    std::shared_ptr<MediaRegistry>& r = document.host().media;
    if (!r) r = std::make_shared<MediaRegistry>();
    return *r;
}

MediaState* find_state(Document& document, const Node& node) {
    std::shared_ptr<MediaRegistry>& r = document.host().media;
    if (!r) return nullptr;
    auto it = r->items.find(&node);
    return it == r->items.end() ? nullptr : it->second.get();
}

std::optional<double> number_attr(const Node& node, std::string_view name) {
    const std::optional<std::string> v = node.attribute(name);
    if (!v || v->empty()) return std::nullopt;
    const std::optional<double> d = parse_float(*v);
    if (!d || !std::isfinite(*d)) return std::nullopt;
    return d;
}

std::optional<double> time_attr(const Node& node, std::string_view name) {
    const std::optional<std::string> v = node.attribute(name);
    if (!v || v->empty()) return std::nullopt;
    std::string text = *v;
    if (!text.empty() && (text.back() == 's' || text.back() == 'S')) text.pop_back();
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) {
        const std::optional<double> d = parse_float(text);
        if (!d || !std::isfinite(*d)) return std::nullopt;
        return d;
    }
    double total = 0.0;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(':', start);
        const std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::optional<double> d = parse_float(part);
        if (!d || !std::isfinite(*d) || *d < 0.0) return std::nullopt;
        total = total * 60.0 + *d;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return total;
}

double range_start_of(const Node& node) {
    return std::max(0.0, time_attr(node, "start").value_or(0.0));
}

double range_end_of(const Node& node) {
    const std::optional<double> e = time_attr(node, "end");
    return e && *e > range_start_of(node) ? *e : -1.0;
}

std::string short_number(double v, int digits) {
    if (!std::isfinite(v)) v = 0.0;
    const double scale = std::pow(10.0, digits);
    v = std::round(v * scale) / scale;
    if (v == 0.0) v = 0.0;
    return format_float(v);
}

void schedule_flush(Document& document, MediaRegistry& r);

void queue_event(Document& document, Node& node, const std::string& type, const std::string& text = std::string()) {
    MediaRegistry& r = registry(document);
    Event e;
    e.type = type;
    e.bubbles = false;
    e.text = text;
    r.queue.emplace_back(&node, std::move(e));
    schedule_flush(document, r);
}

void queue_sized_event(Document& document, Node& node, const std::string& type, double x, double y) {
    MediaRegistry& r = registry(document);
    Event e;
    e.type = type;
    e.bubbles = false;
    e.x = x;
    e.y = y;
    r.queue.emplace_back(&node, std::move(e));
    schedule_flush(document, r);
}

void flush(Document& document) {
    const std::shared_ptr<MediaRegistry> keep = document.host().media;
    if (!keep) return;
    keep->flush_timer = 0;
    for (int round = 0; round < 8 && !keep->queue.empty(); ++round) {
        std::vector<std::pair<Node*, Event>> batch;
        batch.swap(keep->queue);
        for (auto& item : batch) document.events().dispatch(document, item.first, item.second);
    }
}

void schedule_flush(Document& document, MediaRegistry& r) {
    if (r.flush_timer && document.events().has_timer(r.flush_timer)) return;
    Document* d = &document;
    r.flush_timer = document.events().set_timeout(0.0, [d] { flush(*d); });
}

void ensure_polling(Document& document) {
    MediaRegistry& r = registry(document);
    if (r.poll_timer && document.events().has_timer(r.poll_timer)) return;
    Document* d = &document;
    r.poll_timer = document.events().set_interval(50.0, [d] { update_media(*d); });
}

Node* child_with_class(Node& parent, std::string_view name) {
    for (const auto& c : parent.children()) {
        if (c->is_element() && c->has_class(name)) return c.get();
    }
    return nullptr;
}

void set_class(Node& node, const std::string& name, bool on) {
    if (node.has_class(name) == on) return;
    std::vector<std::string> classes = node.classes();
    if (on) {
        classes.push_back(name);
    } else {
        classes.erase(std::remove(classes.begin(), classes.end(), name), classes.end());
    }
    std::string joined;
    for (const std::string& c : classes) {
        if (!joined.empty()) joined += ' ';
        joined += c;
    }
    if (joined.empty()) node.remove_attribute("class");
    else node.set_attribute("class", joined);
}

void set_text_if(Node& node, const std::string& text) {
    if (node.inner_text() != text) node.set_inner_text(text);
}

double effective_end(const MediaState& s) {
    if (s.range_end >= 0.0 && (s.duration <= 0.0 || s.range_end < s.duration)) return s.range_end;
    return s.duration;
}

const char* k_play_label = "\xE2\x96\xB6";
const char* k_pause_label = "\xE2\x9D\x9A\xE2\x9D\x9A";
const char* k_sound_label = "\xE2\x99\xAA";
const char* k_fullscreen_label = "\xE2\xA4\xA2";
constexpr double k_controls_linger_ms = 2500.0;

void update_controls(Document& document, MediaState& s) {
    (void)document;
    Node* row = child_with_class(*s.node, "media-controls");
    if (!row) return;
    const double pos = s.player ? s.player->position() : s.range_start;
    if (Node* play = child_with_class(*row, "media-play")) {
        set_text_if(*play, s.playing ? k_pause_label : k_play_label);
        set_class(*play, "playing", s.playing);
    }
    if (Node* seek = child_with_class(*row, "media-seek")) {
        const double end = effective_end(s);
        seek->set_attribute("min", short_number(s.range_start, 3));
        seek->set_attribute("max", short_number(end > s.range_start ? end : s.range_start + 1.0, 3));
        if (!s.scrubbing) seek->set_attribute("value", short_number(pos, 2));
    }
    if (Node* time = child_with_class(*row, "media-time")) set_text_if(*time, format_media_time(pos) + " / " + format_media_time(effective_end(s)));
    if (Node* mute = child_with_class(*row, "media-mute")) set_class(*mute, "muted", s.muted);
    if (Node* volume = child_with_class(*row, "media-volume")) volume->set_attribute("value", short_number(s.volume, 2));
    if (Node* full = child_with_class(*row, "media-fullscreen")) set_class(*full, "active", s.node->has_class("fullscreen"));
}

void request_paint(Document& document) {
    ++document.host().paint_requests;
}

void show_controls(Document& document, MediaState& s) {
    s.controls_until = document.events().now() + k_controls_linger_ms;
    if (Node* row = child_with_class(*s.node, "media-controls")) set_class(*row, "idle", false);
}

void update_controls_visibility(Document& document, MediaState& s) {
    if (!is_video(*s.node)) return;
    Node* row = child_with_class(*s.node, "media-controls");
    if (!row) return;
    const bool busy = !s.playing || s.scrubbing || row->has_state(StateHover);
    if (busy) s.controls_until = std::max(s.controls_until, document.events().now() + k_controls_linger_ms);
    set_class(*row, "idle", !busy && document.events().now() >= s.controls_until);
}

void check_video_size(Document& document, MediaState& s) {
    double w = 0.0;
    double h = 0.0;
    if (!s.player->video_size(w, h)) return;
    if (w == s.video_width && h == s.video_height) return;
    s.video_width = w;
    s.video_height = h;
    s.node->mark_changed(true);
    queue_sized_event(document, *s.node, "resize", w, h);
}

void forward_player_events(Document& document, MediaState& s) {
    for (const MediaPlayerEvent& e : s.player->take_events()) {
        if (e.type == "error") s.error = e.text;
        queue_event(document, *s.node, e.type, e.text);
    }
}

void frame_tick(Document& document, Node& node);

void schedule_frame(Document& document, MediaState& s) {
    if (!s.player || !s.player->has_video()) return;
    EventRegistry& events = document.events();
    if (s.frame_timer && events.has_timer(s.frame_timer)) events.clear_timer(s.frame_timer);
    s.frame_timer = 0;
    const bool wanted = (s.playing && !s.want_start) || s.player->frame_pending();
    if (!wanted || !document.is_attached(*s.node)) return;
    const double delay = s.player->frame_delay();
    const double ms = delay < 0.0 ? 10.0 : std::max(1.0, std::min(250.0, delay * 1000.0));
    Document* d = &document;
    Node* n = s.node;
    s.frame_timer = events.set_timeout(ms, [d, n] { frame_tick(*d, *n); });
}

void frame_tick(Document& document, Node& node) {
    MediaState* s = find_state(document, node);
    if (!s || !s->player) return;
    s->frame_timer = 0;
    MediaFrame f;
    if (!s->player->planar_frame(f)) s->player->frame(f);
    if (f.serial != s->frame_serial) {
        s->frame_serial = f.serial;
        request_paint(document);
    }
    check_video_size(document, *s);
    forward_player_events(document, *s);
    schedule_frame(document, *s);
}

void kick_frames(Document& document, MediaState& s) {
    if (!s.player || !s.player->has_video()) return;
    if (s.frame_timer && document.events().has_timer(s.frame_timer)) return;
    schedule_frame(document, s);
}

void load_src(Document& document, MediaState& s, bool full) {
    Node& node = *s.node;
    s.src = node.attribute_or("src", "");
    s.meta_sent = false;
    s.ready_sent = false;
    s.error_sent = false;
    s.error.clear();
    s.duration = 0.0;
    s.shown = false;
    s.decoding = false;
    s.video_width = 0.0;
    s.video_height = 0.0;
    s.player->close();
    s.player->set_range(s.range_start, s.range_end);
    if (s.frame_serial != 0) {
        s.frame_serial = 0;
        request_paint(document);
    }
    if (s.src.empty()) return;
    const std::string preload = lower(node.attribute_or("preload", "auto"));
    if (preload == "none" && !full) return;
    s.decoding = full || preload != "metadata";
    s.player->open(media_path(document, s.src), s.decoding);
    ensure_polling(document);
    kick_frames(document, s);
}

void begin(Document& document, MediaState& s) {
    s.want_start = false;
    s.shown = true;
    double pos = s.player->position();
    const double end = effective_end(s);
    if (end > 0.0 && pos >= end - 1e-4) pos = s.range_start;
    std::string err;
    if (!s.player->start(pos, err)) {
        s.playing = false;
        s.error = err.empty() ? std::string("can't play") : err;
        queue_event(document, *s.node, "error", s.error);
        queue_event(document, *s.node, "pause");
    }
    s.last_update = document.events().now();
    update_controls(document, s);
    if (s.player->has_video()) {
        show_controls(document, s);
        schedule_frame(document, s);
    }
}

bool do_play(Document& document, MediaState& s) {
    if (s.src.empty()) {
        s.error = "there's no src to play";
        queue_event(document, *s.node, "error", s.error);
        return false;
    }
    if (!s.playing) {
        s.playing = true;
        queue_event(document, *s.node, "play");
    }
    const MediaLoad state = s.player->load_state();
    if (state == MediaLoad::Ready) {
        begin(document, s);
    } else {
        s.want_start = true;
        if (state == MediaLoad::Idle || state == MediaLoad::Failed) {
            load_src(document, s, true);
            s.want_start = true;
        } else if (state == MediaLoad::Metadata) {
            s.decoding = true;
            s.player->load_fully();
        }
    }
    ensure_polling(document);
    update_controls(document, s);
    return s.playing;
}

void do_pause(Document& document, MediaState& s) {
    s.want_start = false;
    if (!s.playing) return;
    s.playing = false;
    s.player->pause();
    queue_event(document, *s.node, "timeupdate");
    queue_event(document, *s.node, "pause");
    update_controls(document, s);
    if (s.player->has_video()) {
        show_controls(document, s);
        schedule_frame(document, s);
    }
}

bool do_seek(Document& document, MediaState& s, double seconds) {
    if (!std::isfinite(seconds)) return false;
    seconds = std::max(s.range_start, seconds);
    const double end = effective_end(s);
    if (end > 0.0) seconds = std::min(seconds, end);
    queue_event(document, *s.node, "seeking");
    s.player->seek(seconds);
    s.shown = true;
    queue_event(document, *s.node, "timeupdate");
    queue_event(document, *s.node, "seeked");
    s.last_update = document.events().now();
    update_controls(document, s);
    if (s.player->has_video()) schedule_frame(document, s);
    return true;
}

void apply_gain(MediaState& s) {
    s.player->set_gain(s.muted ? 0.0 : s.volume);
}

bool needs_poll(const MediaState& s) {
    if (!s.player) return false;
    if (s.playing) return true;
    const MediaLoad state = s.player->load_state();
    if (state == MediaLoad::Loading) return true;
    if (state == MediaLoad::Metadata && (!s.meta_sent || s.decoding)) return true;
    if (state == MediaLoad::Ready && !s.ready_sent) return true;
    if (state == MediaLoad::Failed && !s.error_sent) return true;
    return false;
}

void apply_attributes(Document& document, MediaState& s) {
    Node& node = *s.node;
    const double start = range_start_of(node);
    const double end = range_end_of(node);
    if (start != s.range_start || end != s.range_end) {
        s.range_start = start;
        s.range_end = end;
        s.player->set_range(start, end);
        update_controls(document, s);
    }
    if (node.attribute_or("src", "") != s.src) {
        const bool was = s.playing;
        if (was) do_pause(document, s);
        load_src(document, s, false);
        queue_event(document, node, "emptied");
        if (media_flag(node, "autoplay")) do_play(document, s);
        update_controls(document, s);
    }
    const double volume = media_volume(node);
    const bool muted = media_muted(node);
    if (volume != s.volume || muted != s.muted) {
        s.volume = volume;
        s.muted = muted;
        apply_gain(s);
        queue_event(document, node, "volumechange");
        update_controls(document, s);
    }
    const double rate = media_rate(node);
    if (rate != s.rate) {
        s.rate = rate;
        s.player->set_rate(rate);
        queue_event(document, node, "ratechange");
    }
    const bool loop = media_loop(node);
    if (loop != s.loop) {
        s.loop = loop;
        s.player->set_loop(loop);
    }
    const bool preserve = media_preserves_pitch(node);
    if (preserve != s.preserve) {
        s.preserve = preserve;
        s.player->set_preserve_pitch(preserve);
    }
}

MediaState& state_for(Document& document, Node& node) {
    MediaRegistry& r = registry(document);
    auto it = r.items.find(&node);
    if (it != r.items.end()) return *it->second;
    auto made = std::make_unique<MediaState>();
    MediaState& s = *made;
    s.node = &node;
    s.player = make_player(document, node);
    s.volume = media_volume(node);
    s.muted = media_muted(node);
    s.rate = media_rate(node);
    s.loop = media_loop(node);
    s.preserve = media_preserves_pitch(node);
    s.range_start = range_start_of(node);
    s.range_end = range_end_of(node);
    r.items[&node] = std::move(made);
    apply_gain(s);
    s.player->set_preserve_pitch(s.preserve);
    s.player->set_rate(s.rate);
    s.player->set_loop(s.loop);
    load_src(document, s, false);
    if (media_flag(node, "autoplay") && !s.src.empty()) do_play(document, s);
    update_controls(document, s);
    return s;
}

double meter_level(Document& document, const Node& meter) {
    const std::string target = meter.attribute_or("for", "master");
    if (target.empty() || target == "master") return audio::master_level();
    auto handle_after = [&](std::string_view prefix) -> std::int64_t {
        return std::strtoll(target.c_str() + prefix.size(), nullptr, 10);
    };
    if (target.rfind("monitor-input:", 0) == 0) return audio::monitor_input_level(handle_after("monitor-input:"));
    if (target.rfind("monitor:", 0) == 0) return audio::monitor_output_level(handle_after("monitor:"));
    Node* n = document.find_by_id(target);
    if (!n || !is_media(*n)) return 0.0;
    MediaState* s = find_state(document, *n);
    return s && s->playing ? s->player->level() : 0.0;
}

void update_meter(Document& document, Node& meter, MeterState& state) {
    const double level = std::max(0.0, meter_level(document, meter));
    state.shown = std::max(level, state.shown * 0.82);
    if (state.shown < 1e-5) state.shown = 0.0;
    double fraction = 0.0;
    if (lower(meter.attribute_or("scale", "db")) == "linear") {
        fraction = std::min(1.0, state.shown);
    } else {
        double floor_db = number_attr(meter, "min-db").value_or(-60.0);
        if (floor_db >= 0.0) floor_db = -60.0;
        fraction = std::max(0.0, std::min(1.0, (audio::level_to_db(state.shown) - floor_db) / -floor_db));
    }
    meter.set_attribute("value", short_number(state.shown, 3));
    if (Node* fill = child_with_class(meter, "meter-fill")) {
        fill->set_attribute("style", "width: " + short_number(std::round(fraction * 200.0) / 2.0, 1) + "%");
        set_class(*fill, "clipping", level >= 0.999);
    }
}

void expand_meter(Document& document, Node& meter) {
    if (child_with_class(meter, "meter-fill")) return;
    Node* fill = document.create_element("div");
    fill->set_attribute("class", "meter-fill");
    fill->set_attribute("style", "width: 0%");
    document.append_child(meter, *fill);
}

void walk_media(Document& document, const std::function<void(Node&)>& on_media, const std::function<void(Node&)>& on_meter) {
    std::vector<Node*> media;
    std::vector<Node*> meters;
    document.root().walk([&](Node& n) {
        if (!n.is_element()) return;
        if (is_media(n)) media.push_back(&n);
        else if (is_audio_meter(n)) meters.push_back(&n);
    });
    for (Node* n : media) on_media(*n);
    for (Node* n : meters) on_meter(*n);
}

} // namespace

std::unique_ptr<MediaPlayer> make_audio_player() {
    return std::make_unique<AudioPlayer>();
}

void register_media_player(const std::string& tag, MediaPlayerFactory factory) {
    factories()[lower(tag)] = std::move(factory);
}

bool is_media_tag(std::string_view tag) noexcept {
    if (tag == "audio" || tag == "video") return true;
    try {
        return factories().count(std::string(tag)) != 0;
    } catch (...) {
        return false;
    }
}

bool is_media(const Node& node) noexcept {
    return node.is_element() && is_media_tag(node.tag());
}

bool is_video(const Node& node) noexcept {
    return node.is_element() && node.tag() == "video";
}

bool is_audio_meter(const Node& node) noexcept {
    return node.is_element() && node.tag() == "audio_meter";
}

Node* media_of(Node& node) noexcept {
    for (Node* n = &node; n; n = n->parent()) {
        if (is_media(*n)) return n;
    }
    return nullptr;
}

bool media_flag(const Node& node, std::string_view name) {
    const Attribute* a = node.find_attribute(name);
    if (!a) return false;
    const std::string v = lower(a->value);
    return !(v == "false" || v == "0" || v == "no" || v == "off");
}

double media_volume(const Node& node) {
    const double v = number_attr(node, "volume").value_or(1.0);
    return std::max(0.0, std::min(1.0, v));
}

double media_rate(const Node& node) {
    const double v = number_attr(node, "playbackrate").value_or(1.0);
    if (v <= 0.0) return 1.0;
    return std::max(0.0625, std::min(16.0, v));
}

bool media_muted(const Node& node) {
    return media_flag(node, "muted");
}

bool media_loop(const Node& node) {
    return media_flag(node, "loop");
}

bool media_preserves_pitch(const Node& node) {
    return !node.has_attribute("preservespitch") || media_flag(node, "preservespitch");
}

std::string media_path(const Document& document, const std::string& src) {
    if (src.empty()) return std::string();
    const std::string base = std::filesystem::path(document.path()).parent_path().string();
    return resolve_path(base, src);
}

std::vector<std::string> video_extensions() {
    return { "mp4", "m4v", "mov", "mkv", "webm", "ogv", "3gp", "ivf", "obu", "hevc", "h265", "265" };
}

std::vector<std::string> video_file_problems(const std::string& src, const std::string& path) {
    std::vector<std::string> out;
    fizmo::video::MediaInfo info;
    if (is_virtual_file(path)) {
        const std::optional<std::string> bytes = read_file(path);
        if (!bytes) return out;
        info = fizmo::video::probe_media(bytes->data(), bytes->size());
    } else {
        info = fizmo::video::probe_media(path);
    }
    if (!info.valid) {
        out.push_back("has src '" + src + "', which Grove can't read" + (info.error.empty() ? std::string() : " (" + info.error + ")"));
        return out;
    }
    if (info.video_track >= 0 && !info.video_supported) {
        std::string playable;
        const std::vector<std::string> codecs = fizmo::video::supported_video_codecs();
        for (std::size_t i = 0; i < codecs.size(); ++i) {
            if (i > 0) playable += i + 1 == codecs.size() ? " and " : ", ";
            playable += fizmo::video::video_codec_display_name(codecs[i]);
        }
        out.push_back("has src '" + src + "' with " + fizmo::video::video_codec_display_name(info.video_codec) + " video, which isn't supported; the video will play without picture (Grove plays " + playable + " video)");
    }
    if (info.audio_track >= 0 && !info.audio_supported) {
        out.push_back("has src '" + src + "' with " + (info.audio_codec.empty() ? std::string("unknown") : info.audio_codec) + " audio, which isn't supported; the video will play without sound");
    }
    return out;
}

std::string format_media_time(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long total = static_cast<long long>(std::floor(seconds + 1e-6));
    const long long h = total / 3600;
    const long long m = (total / 60) % 60;
    const long long s = total % 60;
    char buf[48];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

bool expand_media(Document& document, Node& node) {
    if (is_audio_meter(node)) {
        const bool had = child_with_class(node, "meter-fill") != nullptr;
        expand_meter(document, node);
        return !had;
    }
    if (!is_media(node)) return false;
    Node* existing = child_with_class(node, "media-controls");
    const bool want = media_flag(node, "controls");
    if (!want) {
        if (!existing) return false;
        document.remove(*existing);
        return true;
    }
    if (existing) return false;
    const std::string id = node.id();
    auto part = [&](const char* tag, const char* cls, const char* suffix) {
        Node* n = document.create_element(tag);
        n->set_attribute("class", cls);
        if (!id.empty()) n->set_attribute("id", id + suffix);
        return n;
    };
    Node* row = part("row", "media-controls", "-controls");
    Node* play = part("button", "media-play", "-play");
    play->set_inner_text(k_play_label);
    Node* seek = part("input_slider", "media-seek", "-seek");
    seek->set_attribute("min", "0");
    seek->set_attribute("max", "1");
    seek->set_attribute("step", "0.01");
    seek->set_attribute("value", "0");
    Node* time = part("text", "media-time", "-time");
    time->set_inner_text("0:00 / 0:00");
    Node* mute = part("button", "media-mute", "-mute");
    mute->set_inner_text(k_sound_label);
    Node* volume = part("input_slider", "media-volume", "-volume");
    volume->set_attribute("min", "0");
    volume->set_attribute("max", "1");
    volume->set_attribute("step", "0.01");
    volume->set_attribute("value", short_number(media_volume(node), 2));
    if (media_muted(node)) mute->set_attribute("class", "media-mute muted");
    document.append_child(*row, *play);
    document.append_child(*row, *seek);
    document.append_child(*row, *time);
    document.append_child(*row, *mute);
    document.append_child(*row, *volume);
    if (is_video(node)) {
        Node* full = part("button", node.has_class("fullscreen") ? "media-fullscreen active" : "media-fullscreen", "-fullscreen");
        full->set_inner_text(k_fullscreen_label);
        document.append_child(*row, *full);
    }
    document.append_child(node, *row);
    if (MediaState* s = find_state(document, node)) update_controls(document, *s);
    return true;
}

std::size_t expand_media_elements(Document& document) {
    std::size_t changed = 0;
    walk_media(document, [&](Node& n) { changed += expand_media(document, n) ? 1 : 0; }, [&](Node& n) { changed += expand_media(document, n) ? 1 : 0; });
    return changed;
}

void start_media(Document& document) {
    registry(document).started = true;
    sync_media(document);
}

void sync_media(Document& document) {
    MediaRegistry& r = registry(document);
    std::set<Node*> meters;
    walk_media(document, [&](Node& n) {
        expand_media(document, n);
        if (!r.started && !r.items.count(&n)) return;
        MediaState& s = state_for(document, n);
        apply_attributes(document, s);
    }, [&](Node& n) {
        expand_media(document, n);
        meters.insert(&n);
    });
    for (auto it = r.meters.begin(); it != r.meters.end();) {
        if (!meters.count(it->first)) it = r.meters.erase(it);
        else ++it;
    }
    if (r.started) {
        for (Node* m : meters) r.meters.emplace(m, MeterState{});
    }
    for (auto& kv : r.items) {
        MediaState& s = *kv.second;
        if (s.playing && !document.is_attached(*s.node)) do_pause(document, s);
    }
    bool active = !r.meters.empty() || !r.queue.empty();
    for (auto& kv : r.items) active = active || needs_poll(*kv.second);
    if (active) ensure_polling(document);
}

std::size_t poll_monitors(Document& document) {
    const std::vector<std::int64_t> failed = audio::update_monitors();
    if (failed.empty()) return 0;
    std::vector<Node*> meters;
    document.root().walk([&meters](Node& n) {
        if (is_audio_meter(n)) meters.push_back(&n);
    });
    std::size_t fired = 0;
    for (std::int64_t id : failed) {
        const std::string text = audio::monitor_error(id);
        const std::string number = std::to_string(id);
        for (Node* meter : meters) {
            const std::string target = meter->attribute_or("for", "");
            if (target != "monitor:" + number && target != "monitor-input:" + number) continue;
            queue_event(document, *meter, "error", text);
            ++fired;
        }
    }
    return fired;
}

void update_media(Document& document) {
    const std::shared_ptr<MediaRegistry> keep = document.host().media;
    if (!keep) return;
    poll_monitors(document);
    MediaRegistry& r = *keep;
    const double now = document.events().now();
    bool active = false;
    for (auto& kv : r.items) {
        MediaState& s = *kv.second;
        if (!s.player) continue;
        bool changed = false;
        if (s.playing && !document.is_attached(*s.node)) do_pause(document, s);
        const MediaLoad state = s.player->load_state();
        if ((state == MediaLoad::Metadata || state == MediaLoad::Ready) && !s.meta_sent) {
            s.meta_sent = true;
            s.duration = s.player->duration();
            if (s.duration > 0.0) queue_event(document, *s.node, "durationchange");
            queue_event(document, *s.node, "loadedmetadata");
            changed = true;
        }
        if (state == MediaLoad::Ready && !s.ready_sent) {
            s.ready_sent = true;
            const double d = s.player->duration();
            if (std::fabs(d - s.duration) > 1e-3) {
                s.duration = d;
                queue_event(document, *s.node, "durationchange");
            }
            queue_event(document, *s.node, "canplay");
            if (s.want_start && s.playing) begin(document, s);
            changed = true;
        }
        if (state == MediaLoad::Failed && !s.error_sent) {
            s.error_sent = true;
            s.error = s.player->error();
            queue_event(document, *s.node, "error", s.error);
            if (s.playing) {
                s.playing = false;
                s.want_start = false;
                queue_event(document, *s.node, "pause");
            }
            changed = true;
        }
        if (s.playing && !s.want_start) {
            if (s.player->finished()) {
                s.playing = false;
                queue_event(document, *s.node, "timeupdate");
                queue_event(document, *s.node, "pause");
                queue_event(document, *s.node, "ended");
                changed = true;
            } else if (now - s.last_update >= 250.0) {
                s.last_update = now;
                queue_event(document, *s.node, "timeupdate");
                changed = true;
            }
        }
        if (s.player->has_video()) {
            check_video_size(document, s);
            forward_player_events(document, s);
            update_controls_visibility(document, s);
            kick_frames(document, s);
        }
        if (changed) update_controls(document, s);
        active = active || needs_poll(s);
    }
    for (auto& kv : r.meters) {
        if (!document.is_attached(*kv.first)) continue;
        update_meter(document, *kv.first, kv.second);
        active = true;
    }
    flush(document);
    if (!active && r.queue.empty() && r.poll_timer) {
        document.events().clear_timer(r.poll_timer);
        r.poll_timer = 0;
    }
}

void stop_media(Document& document) {
    const std::shared_ptr<MediaRegistry> keep = document.host().media;
    if (!keep) return;
    for (auto& kv : keep->items) {
        if (kv.second->player) kv.second->player->close();
        kv.second->playing = false;
        if (kv.second->frame_timer) document.events().clear_timer(kv.second->frame_timer);
        kv.second->frame_timer = 0;
    }
    keep->items.clear();
    keep->meters.clear();
    keep->queue.clear();
    if (keep->poll_timer) document.events().clear_timer(keep->poll_timer);
    if (keep->flush_timer) document.events().clear_timer(keep->flush_timer);
    keep->poll_timer = 0;
    keep->flush_timer = 0;
    keep->started = false;
}

bool media_event(Document& document, Node& target, const Event& event) {
    if (event.key == "Escape" && normalize_event_type(event.type) == "keydown") {
        const std::shared_ptr<MediaRegistry> keep = document.host().media;
        if (!keep) return false;
        std::vector<Node*> full;
        for (auto& kv : keep->items) {
            if (kv.second->node->has_class("fullscreen") && document.is_attached(*kv.second->node)) full.push_back(kv.second->node);
        }
        for (Node* n : full) media_set_fullscreen(document, *n, false);
        return !full.empty();
    }
    if (is_video(target) && media_flag(target, "controls")) {
        const std::string kind = normalize_event_type(event.type);
        if (kind == "click" && event.button <= 1) {
            if (media_is_playing(document, target)) return media_pause(document, target);
            return media_play(document, target);
        }
        if (kind == "doubleclick" && event.button <= 1) return media_toggle_fullscreen(document, target);
        return false;
    }
    Node* row = target.parent();
    if (!row || !row->has_class("media-controls")) return false;
    Node* media = row->parent();
    if (!media || !is_media(*media)) return false;
    const std::string type = normalize_event_type(event.type);
    if (type != "click" && type != "input" && type != "change") return false;
    if (type == "click" && target.has_class("media-play")) {
        if (media_is_playing(document, *media)) return media_pause(document, *media);
        return media_play(document, *media);
    }
    if (type == "click" && target.has_class("media-mute")) return media_set_muted(document, *media, !media_muted(*media));
    if (type == "click" && target.has_class("media-fullscreen")) return media_toggle_fullscreen(document, *media);
    if (target.has_class("media-seek") && type != "click") {
        const std::optional<double> v = input_float(target);
        if (!v) return false;
        MediaState& s = state_for(document, *media);
        s.scrubbing = type == "input";
        return do_seek(document, s, *v);
    }
    if (target.has_class("media-volume") && type != "click") {
        const std::optional<double> v = input_float(target);
        if (!v) return false;
        media_set_volume(document, *media, *v);
        if (*v > 0.0 && media_muted(*media)) media_set_muted(document, *media, false);
        return true;
    }
    return false;
}

bool media_play(Document& document, Node& node) {
    if (!is_media(node)) return false;
    return do_play(document, state_for(document, node));
}

bool media_pause(Document& document, Node& node) {
    if (!is_media(node)) return false;
    do_pause(document, state_for(document, node));
    return true;
}

bool media_stop(Document& document, Node& node) {
    if (!is_media(node)) return false;
    MediaState& s = state_for(document, node);
    do_pause(document, s);
    s.player->seek(s.range_start);
    queue_event(document, node, "timeupdate");
    update_controls(document, s);
    return true;
}

bool media_seek(Document& document, Node& node, double seconds) {
    if (!is_media(node)) return false;
    return do_seek(document, state_for(document, node), seconds);
}

double media_position(Document& document, Node& node) {
    if (!is_media(node)) return 0.0;
    MediaState* s = find_state(document, node);
    if (!s) return range_start_of(node);
    return s->player->position();
}

double media_duration(Document& document, Node& node) {
    if (!is_media(node)) return 0.0;
    MediaState* s = find_state(document, node);
    if (!s) return 0.0;
    const double d = s->player->duration();
    return d > 0.0 ? d : s->duration;
}

bool media_is_playing(Document& document, Node& node) {
    if (!is_media(node)) return false;
    MediaState* s = find_state(document, node);
    return s && s->playing;
}

bool media_set_volume(Document& document, Node& node, double volume) {
    if (!is_media(node) || !std::isfinite(volume)) return false;
    node.set_attribute("volume", short_number(std::max(0.0, std::min(1.0, volume)), 4));
    document.touch();
    apply_attributes(document, state_for(document, node));
    return true;
}

bool media_set_muted(Document& document, Node& node, bool muted) {
    if (!is_media(node)) return false;
    if (muted) node.set_attribute("muted", "");
    else node.remove_attribute("muted");
    document.touch();
    apply_attributes(document, state_for(document, node));
    return true;
}

bool media_set_loop(Document& document, Node& node, bool loop) {
    if (!is_media(node)) return false;
    if (loop) node.set_attribute("loop", "");
    else node.remove_attribute("loop");
    document.touch();
    apply_attributes(document, state_for(document, node));
    return true;
}

bool media_set_rate(Document& document, Node& node, double rate) {
    if (!is_media(node) || !std::isfinite(rate) || rate <= 0.0) return false;
    node.set_attribute("playbackrate", short_number(rate, 4));
    document.touch();
    apply_attributes(document, state_for(document, node));
    return true;
}

bool media_set_src(Document& document, Node& node, const std::string& src) {
    if (!is_media(node)) return false;
    node.set_attribute("src", src);
    document.touch();
    MediaState* existing = find_state(document, node);
    if (!existing) {
        state_for(document, node);
        return true;
    }
    apply_attributes(document, *existing);
    return true;
}

std::string media_error(Document& document, Node& node) {
    if (!is_media(node)) return std::string();
    MediaState* s = find_state(document, node);
    return s ? s->error : std::string();
}

double media_level(Document& document, Node& node) {
    if (is_audio_meter(node)) return meter_level(document, node);
    if (!is_media(node)) return 0.0;
    MediaState* s = find_state(document, node);
    return s && s->playing ? s->player->level() : 0.0;
}

bool media_video_size(Document& document, const Node& node, double& width, double& height) {
    if (!is_video(node)) return false;
    MediaState* s = find_state(document, node);
    if (!s || !s->player) return false;
    if (s->video_width > 0.0 && s->video_height > 0.0) {
        width = s->video_width;
        height = s->video_height;
        return true;
    }
    return s->player->video_size(width, height);
}

bool media_frame(Document& document, const Node& node, MediaFrame& out) {
    out = MediaFrame{};
    if (!is_video(node)) return false;
    MediaState* s = find_state(document, node);
    if (!s || !s->player) return false;
    const bool got = s->player->frame(out);
    if (out.serial != s->frame_serial) s->frame_serial = out.serial;
    return got;
}

bool media_planar_frame(Document& document, const Node& node, MediaFrame& out) {
    out = MediaFrame{};
    if (!is_video(node)) return false;
    MediaState* s = find_state(document, node);
    if (!s || !s->player) return false;
    const bool got = s->player->planar_frame(out);
    if (out.serial != s->frame_serial) s->frame_serial = out.serial;
    return got && out.planes;
}

bool media_snapshot(Document& document, const Node& node, std::vector<std::uint8_t>& rgba, unsigned& width, unsigned& height) {
    MediaFrame f;
    if (!media_frame(document, node, f) || !f.rgba) return false;
    media_frame_size(f, width, height);
    rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    copy_media_frame(f, rgba.data());
    return true;
}

bool media_snapshot_to_canvas(Document& document, const Node& node, Node& canvas) {
    if (!is_canvas(canvas)) return false;
    std::vector<std::uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    if (!media_snapshot(document, node, rgba, w, h)) return false;
    return set_canvas_pixels(document, canvas, w, h, std::move(rgba));
}

bool media_set_fullscreen(Document& document, Node& node, bool fullscreen) {
    if (!is_video(node)) return false;
    if (node.has_class("fullscreen") == fullscreen) return true;
    set_class(node, "fullscreen", fullscreen);
    document.touch();
    queue_event(document, node, "fullscreenchange");
    MediaState& s = state_for(document, node);
    update_controls(document, s);
    show_controls(document, s);
    request_paint(document);
    ensure_polling(document);
    return true;
}

bool media_is_fullscreen(const Node& node) {
    return is_video(node) && node.has_class("fullscreen");
}

bool media_toggle_fullscreen(Document& document, Node& node) {
    return media_set_fullscreen(document, node, !media_is_fullscreen(node));
}

std::string media_poster(Document& document, const Node& node) {
    if (!is_video(node)) return std::string();
    const std::string poster = node.attribute_or("poster", "");
    if (poster.empty()) return std::string();
    MediaState* s = find_state(document, node);
    if (s && s->shown && s->frame_serial != 0) return std::string();
    return media_path(document, poster);
}

std::string media_fit(const Node& node) {
    const std::string fit = lower(node.attribute_or("fit", ""));
    if (fit == "cover" || fit == "fill" || fit == "none" || fit == "scale-down") return fit;
    return "contain";
}

void media_pointer(Document& document, Node& node) {
    Node* media = media_of(node);
    if (!media || !is_video(*media)) return;
    MediaState* s = find_state(document, *media);
    if (!s) return;
    const bool hidden = [&] {
        const Node* row = child_with_class(*media, "media-controls");
        return row && row->has_class("idle");
    }();
    s->controls_until = document.events().now() + k_controls_linger_ms;
    if (hidden) show_controls(document, *s);
    if (s->playing) ensure_polling(document);
}

std::vector<std::string> media_problems(const Document& document, const Node& node) {
    std::vector<std::string> out;
    if (is_audio_meter(node)) {
        const std::string target = node.attribute_or("for", "master");
        if (target.empty() || target == "master" || target.rfind("monitor:", 0) == 0 || target.rfind("monitor-input:", 0) == 0) return out;
        const Node* n = document.find_by_id(target);
        if (!n) out.push_back("has for=\"" + target + "\", but there's no element with that id (use an <audio> or <video> id, master, monitor:<id> or monitor-input:<id>)");
        else if (!is_media(*n)) out.push_back("has for=\"" + target + "\", which is a <" + n->tag() + ">, not an <audio> or <video>");
        const std::string scale = lower(node.attribute_or("scale", "db"));
        if (scale != "db" && scale != "linear") out.push_back("has scale=\"" + scale + "\"; use db or linear");
        return out;
    }
    if (!is_media(node)) return out;
    const std::string src = node.attribute_or("src", "");
    if (node.has_attribute("src") && src.empty()) out.push_back("has an empty src");
    if (!src.empty()) {
        const std::string path = media_path(document, src);
        const bool found = path_exists(path);
        if (!found) out.push_back(std::string("can't find the ") + (is_video(node) ? "video" : "audio") + " file '" + src + "'");
        if (is_video(node)) {
            const std::string ext = lower(std::filesystem::path(src).extension().string());
            bool known = false;
            for (const std::string& e : video_extensions()) known = known || "." + e == ext;
            for (const std::string& e : fizmo::audio::readable_audio_extensions()) known = known || "." + lower(e) == ext;
            if (!ext.empty() && !known) {
                std::string list;
                for (const std::string& e : video_extensions()) list += (list.empty() ? "" : ", ") + e;
                out.push_back("has src '" + src + "', which isn't a kind of video file Grove can play (" + list + ")");
            } else if (found) {
                for (const std::string& p : video_file_problems(src, path)) out.push_back(p);
            }
        }
        if (node.tag() == "audio") {
            const std::string ext = lower(std::filesystem::path(src).extension().string());
            bool known = false;
            for (const std::string& e : fizmo::audio::readable_audio_extensions()) {
                std::string with = e;
                if (!with.empty() && with[0] != '.') with = "." + with;
                known = known || lower(with) == ext;
            }
            if (!ext.empty() && !known) {
                std::string list;
                for (const std::string& e : fizmo::audio::readable_audio_extensions()) list += (list.empty() ? "" : ", ") + e;
                out.push_back("has src '" + src + "', which isn't a kind of audio file Grove can play (" + list + ")");
            }
        }
    }
    if (node.has_attribute("volume")) {
        const std::optional<double> v = number_attr(node, "volume");
        if (!v) out.push_back("has volume=\"" + node.attribute_or("volume", "") + "\", which isn't a number");
        else if (*v < 0.0 || *v > 1.0) out.push_back("has volume=\"" + node.attribute_or("volume", "") + "\"; volume goes from 0 to 1");
    }
    if (node.has_attribute("playbackrate")) {
        const std::optional<double> v = number_attr(node, "playbackrate");
        if (!v || *v <= 0.0) out.push_back("has playbackrate=\"" + node.attribute_or("playbackrate", "") + "\", which isn't a number above 0");
        else if (*v < 0.0625 || *v > 16.0) out.push_back("has playbackrate=\"" + node.attribute_or("playbackrate", "") + "\"; it's kept between 0.0625 and 16");
    }
    if (node.has_attribute("preload")) {
        const std::string p = lower(node.attribute_or("preload", ""));
        if (p != "none" && p != "metadata" && p != "auto" && !p.empty()) out.push_back("has preload=\"" + p + "\"; use none, metadata or auto");
    }
    for (const char* name : { "start", "end" }) {
        if (node.has_attribute(name) && !time_attr(node, name)) out.push_back(std::string("has ") + name + "=\"" + node.attribute_or(name, "") + "\", which isn't a time (seconds, or m:ss)");
    }
    const std::optional<double> s = time_attr(node, "start");
    const std::optional<double> e = time_attr(node, "end");
    if (s && *s < 0.0) out.push_back("has a start before 0");
    if (s && e && *e <= *s) out.push_back("has an end that isn't after its start");
    if (is_video(node)) {
        if (node.has_attribute("fit")) {
            const std::string fit = lower(node.attribute_or("fit", ""));
            if (fit != "contain" && fit != "cover" && fit != "fill" && fit != "none" && fit != "scale-down") out.push_back("has fit=\"" + fit + "\"; use contain, cover, fill, none or scale-down");
        }
        const std::string poster = node.attribute_or("poster", "");
        if (node.has_attribute("poster") && poster.empty()) out.push_back("has an empty poster");
        if (!poster.empty() && !read_file(media_path(document, poster))) out.push_back("can't find the poster image '" + poster + "'");
        for (const char* name : { "width", "height" }) {
            if (!node.has_attribute(name)) continue;
            const std::optional<double> v = number_attr(node, name);
            if (!v || *v < 0.0) out.push_back(std::string("has ") + name + "=\"" + node.attribute_or(name, "") + "\", which isn't a size in pixels");
        }
    }
    return out;
}

} // namespace grove
