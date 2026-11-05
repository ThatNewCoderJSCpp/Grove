#ifndef GROVE_MEDIA_HPP
#define GROVE_MEDIA_HPP

#include "markup/events.hpp"
#include "markup/node.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fizmo {
namespace video {
struct VideoFrame;
} // namespace video
} // namespace fizmo

namespace grove {

enum class MediaLoad {
    Idle,
    Loading,
    Metadata,
    Ready,
    Failed
};

struct MediaFrame {
    const std::uint8_t* rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    std::uint64_t serial = 0;
    int rotation = 0;
    double aspect = 0.0;
    const fizmo::video::VideoFrame* planes = nullptr;
};

struct MediaPlayerEvent {
    std::string type;
    std::string text;
};

class MediaPlayer {
public:
    virtual ~MediaPlayer() = default;

    virtual void open(const std::string& path, bool full) = 0;
    virtual void load_fully() = 0;
    virtual void close() = 0;
    virtual MediaLoad load_state() const = 0;
    virtual std::string error() const = 0;
    virtual double duration() const = 0;

    virtual bool start(double position, std::string& error) = 0;
    virtual void pause() = 0;
    virtual void seek(double position) = 0;
    virtual double position() const = 0;
    virtual bool finished() = 0;

    virtual void set_gain(double gain) = 0;
    virtual void set_rate(double rate) = 0;
    virtual void set_loop(bool loop) = 0;
    virtual void set_range(double start, double end) = 0;
    virtual double level() const = 0;
    virtual void set_preserve_pitch(bool preserve) { (void)preserve; }

    virtual bool has_video() const { return false; }
    virtual bool video_size(double& width, double& height) const {
        (void)width;
        (void)height;
        return false;
    }
    virtual bool frame(MediaFrame& out) {
        (void)out;
        return false;
    }
    virtual bool planar_frame(MediaFrame& out) {
        (void)out;
        return false;
    }
    virtual double frame_delay() const { return -1.0; }
    virtual bool frame_pending() const { return false; }
    virtual std::vector<MediaPlayerEvent> take_events() { return {}; }
};

using MediaPlayerFactory = std::function<std::unique_ptr<MediaPlayer>(Document& document, Node& element)>;

void register_media_player(const std::string& tag, MediaPlayerFactory factory);
std::unique_ptr<MediaPlayer> make_audio_player();

std::unique_ptr<MediaPlayer> make_video_player();

bool is_media_tag(std::string_view tag) noexcept;
bool is_media(const Node& node) noexcept;
bool is_video(const Node& node) noexcept;
bool is_audio_meter(const Node& node) noexcept;
Node* media_of(Node& node) noexcept;

bool expand_media(Document& document, Node& node);
std::size_t expand_media_elements(Document& document);
void start_media(Document& document);
void sync_media(Document& document);
void update_media(Document& document);
std::size_t poll_monitors(Document& document);
void stop_media(Document& document);
bool media_event(Document& document, Node& target, const Event& event);

bool media_play(Document& document, Node& node);
bool media_pause(Document& document, Node& node);
bool media_stop(Document& document, Node& node);
bool media_seek(Document& document, Node& node, double seconds);
double media_position(Document& document, Node& node);
double media_duration(Document& document, Node& node);
bool media_is_playing(Document& document, Node& node);
bool media_set_volume(Document& document, Node& node, double volume);
bool media_set_muted(Document& document, Node& node, bool muted);
bool media_set_loop(Document& document, Node& node, bool loop);
bool media_set_rate(Document& document, Node& node, double rate);
bool media_set_src(Document& document, Node& node, const std::string& src);
std::string media_error(Document& document, Node& node);
double media_level(Document& document, Node& node);

bool media_video_size(Document& document, const Node& node, double& width, double& height);
bool media_frame(Document& document, const Node& node, MediaFrame& out);
bool media_planar_frame(Document& document, const Node& node, MediaFrame& out);
void set_media_planar_frames(bool enabled) noexcept;
bool media_planar_frames() noexcept;
bool media_snapshot(Document& document, const Node& node, std::vector<std::uint8_t>& rgba, unsigned& width, unsigned& height);
bool media_snapshot_to_canvas(Document& document, const Node& node, Node& canvas);
bool media_set_fullscreen(Document& document, Node& node, bool fullscreen);
bool media_is_fullscreen(const Node& node);
bool media_toggle_fullscreen(Document& document, Node& node);
std::string media_poster(Document& document, const Node& node);
std::string media_fit(const Node& node);
void media_pointer(Document& document, Node& node);
void media_frame_size(const MediaFrame& frame, unsigned& width, unsigned& height);
void copy_media_frame(const MediaFrame& frame, std::uint8_t* rgba);

double media_volume(const Node& node);
double media_rate(const Node& node);
bool media_muted(const Node& node);
bool media_loop(const Node& node);
bool media_preserves_pitch(const Node& node);
bool media_flag(const Node& node, std::string_view name);
std::string media_path(const Document& document, const std::string& src);
std::string format_media_time(double seconds);
std::vector<std::string> video_extensions();
std::vector<std::string> video_file_problems(const std::string& src, const std::string& path);

std::vector<std::string> media_problems(const Document& document, const Node& node);

} // namespace grove

#endif // GROVE_MEDIA_HPP
