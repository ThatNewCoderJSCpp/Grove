#include "grove/grove.hpp"
#include "grove/media.hpp"
#include "grove/audio.hpp"
#include "grove/canvas.hpp"
#include "grove/api/grove_api.h"

#include <fizmo/../../Source/Audio/audio.hpp>
#include <fizmo/../../Source/Video/frame.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::cerr << "grove_video_tests.cpp:" << line << ": FAILED: " << what << '\n';
}

#define CHECK(expr) check((expr), #expr, __LINE__)

namespace fs = std::filesystem;

fs::path g_media;
fs::path g_dir;

struct Clock {
    double now = 0.0;
};

struct Recorder {
    std::vector<std::string> events;
    std::vector<std::string> texts;
    double resize_x = 0.0;
    double resize_y = 0.0;

    void listen(grove::Document& doc, grove::Node* node) {
        for (const char* type : { "play", "pause", "ended", "timeupdate", "durationchange", "loadedmetadata", "loadeddata", "canplay", "volumechange", "ratechange", "seeking", "seeked", "error", "emptied", "resize", "warning", "playing", "waiting", "fullscreenchange" }) {
            doc.events().add_listener(node, type, [this](grove::Event& e) {
                events.push_back(e.type);
                texts.push_back(e.text);
                if (e.type == "resize") {
                    resize_x = e.x;
                    resize_y = e.y;
                }
            });
        }
    }

    bool has(const std::string& type) const {
        for (const std::string& e : events) {
            if (e == type) return true;
        }
        return false;
    }

    int count(const std::string& type) const {
        int n = 0;
        for (const std::string& e : events) n += e == type ? 1 : 0;
        return n;
    }

    std::string text_of(const std::string& type) const {
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (events[i] == type) return texts[i];
        }
        return std::string();
    }

    std::vector<std::string> without_timeupdates() const {
        std::vector<std::string> out;
        for (const std::string& e : events) {
            if (e != "timeupdate") out.push_back(e);
        }
        return out;
    }
};

void pump(grove::Document& doc, Clock& clock, double ms) {
    clock.now += ms;
    doc.events().run_due_timers();
}

bool wait_until(grove::Document& doc, Clock& clock, const std::function<bool()>& done) {
    for (int i = 0; i < 3000; ++i) {
        pump(doc, clock, 10.0);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

void play_for(double seconds, grove::Document& doc, Clock& clock, const std::function<void()>& each = nullptr) {
    const double step = 0.02;
    for (double t = 0.0; t < seconds - 1e-9; t += step) {
        grove::audio::advance(step);
        pump(doc, clock, step * 1000.0);
        if (each) each();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

bool play_until(grove::Document& doc, Clock& clock, const std::function<bool()>& done) {
    for (int i = 0; i < 1500; ++i) {
        grove::audio::advance(0.02);
        pump(doc, clock, 20.0);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    return done();
}

bool contains(const std::vector<std::string>& list, const std::vector<std::string>& wanted) {
    std::size_t at = 0;
    for (const std::string& e : list) {
        if (at < wanted.size() && e == wanted[at]) ++at;
    }
    return at == wanted.size();
}

std::string joined(const std::vector<std::string>& list) {
    std::string out;
    for (const std::string& e : list) out += (out.empty() ? "" : " ") + e;
    return out;
}

grove::Project load(const std::string& source, const std::string& name) {
    return grove::load_project_from_source(source, (g_media / name).string(), g_media.string());
}

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
};

Pixel pixel_at(const std::vector<std::uint8_t>& rgba, unsigned width, unsigned x, unsigned y) {
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
    if (i + 3 >= rgba.size()) return Pixel{};
    return Pixel{ rgba[i], rgba[i + 1], rgba[i + 2] };
}

bool is_red(const Pixel& p) { return p.r > 180 && p.g < 80 && p.b < 80; }
bool is_blue(const Pixel& p) { return p.b > 180 && p.r < 80 && p.g < 80; }

bool has_problem(const std::vector<std::string>& problems, const std::string& part) {
    for (const std::string& p : problems) {
        if (p.find(part) != std::string::npos) return true;
    }
    return false;
}

void test_parsing_and_controls() {
    grove::Project project = load(
        "<body>"
        "<video id=\"v\" src=\"clip.mp4\" controls poster=\"poster.png\" fit=\"cover\" width=\"320\" loop muted volume=\"0.5\" playbackrate=\"1.5\" start=\"0.5\" end=\"1:00\"></video>"
        "<video id=\"plain\" src=\"clip.mp4\" />"
        "<audio id=\"a\" src=\"tone.m4a\" controls />"
        "</body>",
        "parse.grove");
    grove::Document& doc = project.document;
    grove::Node* v = doc.find_by_id("v");
    grove::Node* plain = doc.find_by_id("plain");
    CHECK(v && grove::is_media(*v) && grove::is_video(*v));
    CHECK(grove::is_media_tag("video") && grove::is_media_tag("audio") && !grove::is_media_tag("img"));
    CHECK(!grove::is_video(*doc.find_by_id("a")));
    CHECK(grove::tag_role("video") == grove::TagRole::Block);
    CHECK(grove::is_known_tag("video"));
    CHECK(doc.find_by_id("v-controls") && doc.find_by_id("v-controls")->has_class("media-controls"));
    CHECK(doc.find_by_id("v-play") && doc.find_by_id("v-seek") && doc.find_by_id("v-time") && doc.find_by_id("v-mute") && doc.find_by_id("v-volume"));
    CHECK(doc.find_by_id("v-fullscreen") && doc.find_by_id("v-fullscreen")->tag() == "button" && doc.find_by_id("v-fullscreen")->has_class("media-fullscreen"));
    CHECK(!doc.find_by_id("a-fullscreen"));
    CHECK(plain->children().empty());
    CHECK(grove::media_fit(*v) == "cover" && grove::media_fit(*plain) == "contain");
    CHECK(grove::media_volume(*v) == 0.5 && grove::media_rate(*v) == 1.5 && grove::media_muted(*v) && grove::media_loop(*v));
    CHECK(grove::media_poster(doc, *v) == (g_media / "poster.png").lexically_normal().string());
    CHECK(grove::media_poster(doc, *plain).empty());
    CHECK(!grove::media_is_fullscreen(*v));
    CHECK(project.diagnostics.ok());
    std::vector<std::uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    CHECK(!grove::media_snapshot(doc, *v, rgba, w, h));
    double vw = 0.0;
    double vh = 0.0;
    CHECK(!grove::media_video_size(doc, *v, vw, vh));
}

void test_check_warnings() {
    grove::Project project = load(
        "<body>"
        "<video id=\"good\" src=\"clip.mp4\" poster=\"poster.png\" controls />"
        "<video id=\"missing\" src=\"nothing.mp4\" />"
        "<video id=\"text\" src=\"notes.txt\" />"
        "<video id=\"vp8\" src=\"vp8.webm\" />"
        "<video id=\"vp9\" src=\"vp9.webm\" />"
        "<video id=\"ivf\" src=\"vp9.ivf\" />"
        "<video id=\"fit\" src=\"clip.mp4\" fit=\"stretch\" poster=\"nope.png\" width=\"wide\" />"
        "<audio_meter for=\"good\" />"
        "</body>",
        "check.grove");
    grove::Document& doc = project.document;
    CHECK(grove::media_problems(doc, *doc.find_by_id("good")).empty());
    const std::vector<std::string> missing = grove::media_problems(doc, *doc.find_by_id("missing"));
    CHECK(has_problem(missing, "can't find the video file 'nothing.mp4'"));
    CHECK(has_problem(grove::media_problems(doc, *doc.find_by_id("text")), "isn't a kind of video file"));
    const std::vector<std::string> vp8 = grove::media_problems(doc, *doc.find_by_id("vp8"));
    CHECK(has_problem(vp8, "VP8") && has_problem(vp8, "without picture"));
    CHECK(has_problem(vp8, "VP9") && has_problem(vp8, "H.264"));
    CHECK(!has_problem(vp8, "without sound"));
    CHECK(grove::media_problems(doc, *doc.find_by_id("vp9")).empty());
    CHECK(grove::media_problems(doc, *doc.find_by_id("ivf")).empty());
    const std::vector<std::string> fit = grove::media_problems(doc, *doc.find_by_id("fit"));
    CHECK(has_problem(fit, "fit=\"stretch\""));
    CHECK(has_problem(fit, "can't find the poster image 'nope.png'"));
    CHECK(has_problem(fit, "width=\"wide\""));
    bool vp8_warned = false;
    bool good_warned = false;
    for (const grove::Diagnostic& d : project.diagnostics.items()) {
        if (d.message.find("<video id=\"vp8\">") != std::string::npos && d.message.find("VP8") != std::string::npos) vp8_warned = true;
        if (d.message.find("<video id=\"good\">") != std::string::npos || d.message.find("<audio_meter") != std::string::npos) good_warned = true;
        if (d.message.find("<video id=\"vp9\">") != std::string::npos || d.message.find("<video id=\"ivf\">") != std::string::npos) good_warned = true;
    }
    CHECK(vp8_warned);
    CHECK(!good_warned);
    for (const std::string& p : vp8) std::cout << "  vp8 check: " << p << '\n';
}

void test_planar_frames() {
    grove::set_media_planar_frames(true);
    CHECK(grove::media_planar_frames());
    {
        grove::Project project = load("<body><video id=\"v\" src=\"clip.mp4\" /></body>", "planar.grove");
        grove::Document& doc = project.document;
        Clock clock;
        doc.events().set_clock([&clock] { return clock.now; });
        grove::Node* v = doc.find_by_id("v");
        Recorder rec;
        rec.listen(doc, v);
        grove::start_media(doc);
        CHECK(wait_until(doc, clock, [&] { return rec.has("loadeddata"); }));
        grove::MediaFrame f;
        CHECK(grove::media_planar_frame(doc, *v, f) && f.planes && f.width == 160 && f.height == 96 && f.serial > 0 && !f.rgba);
        grove::MediaFrame rgba;
        CHECK(grove::media_frame(doc, *v, rgba) && rgba.rgba && rgba.width == 160 && rgba.height == 96);
        if (f.planes && rgba.rgba && rgba.serial == f.serial) {
            const std::vector<std::uint8_t> want = fizmo::video::to_rgba(*f.planes);
            CHECK(want.size() == static_cast<std::size_t>(160) * 96 * 4 && std::equal(want.begin(), want.end(), rgba.rgba));
        }
        CHECK(grove::media_play(doc, *v));
        std::set<std::uint64_t> serials;
        play_for(0.6, doc, clock, [&] {
            grove::MediaFrame p;
            if (grove::media_planar_frame(doc, *v, p) && p.planes) serials.insert(p.serial);
        });
        CHECK(serials.size() >= 3);
        grove::stop_media(doc);
    }
    grove::set_media_planar_frames(false);
    CHECK(!grove::media_planar_frames());
}

void test_metadata_events_and_frames() {
    grove::Project project = load("<body><video id=\"v\" src=\"clip.mp4\" controls /></body>", "play.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    Recorder rec;
    rec.listen(doc, v);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("canplay") && rec.has("loadeddata") && rec.has("resize"); }));
    CHECK(contains(rec.events, { "loadedmetadata", "canplay" }));
    CHECK(std::fabs(grove::media_duration(doc, *v) - 2.0) < 0.05);
    double w = 0.0;
    double h = 0.0;
    CHECK(grove::media_video_size(doc, *v, w, h) && w == 160.0 && h == 96.0);
    CHECK(rec.resize_x == 160.0 && rec.resize_y == 96.0);
    CHECK(doc.find_by_id("v-time")->inner_text() == "0:00 / 0:02");
    grove::MediaFrame first;
    CHECK(grove::media_frame(doc, *v, first) && first.width == 160 && first.height == 96 && first.serial > 0);
    const std::uint64_t paints = doc.host().paint_requests;
    CHECK(paints > 0);

    CHECK(grove::media_play(doc, *v));
    CHECK(grove::media_is_playing(doc, *v));
    std::set<std::uint64_t> serials;
    play_for(1.0, doc, clock, [&] {
        grove::MediaFrame f;
        if (grove::media_frame(doc, *v, f)) serials.insert(f.serial);
    });
    const double p = grove::media_position(doc, *v);
    CHECK(p > 0.6 && p < 1.3);
    if (!(p > 0.6 && p < 1.3)) std::cerr << "position after 1s: " << p << '\n';
    CHECK(serials.size() >= 5);
    if (serials.size() < 5) std::cerr << "distinct frames while playing: " << serials.size() << '\n';
    CHECK(doc.host().paint_requests > paints + 3);
    CHECK(doc.find_by_id("v-play")->has_class("playing"));
    CHECK(rec.has("playing"));
    CHECK(play_until(doc, clock, [&] { return rec.has("ended"); }));
    CHECK(!grove::media_is_playing(doc, *v));
    CHECK(std::fabs(grove::media_position(doc, *v) - 2.0) < 0.15);
    const std::vector<std::string> order = rec.without_timeupdates();
    CHECK(contains(order, { "loadedmetadata", "canplay", "play", "pause", "ended" }));
    CHECK(rec.count("timeupdate") >= 3);
    CHECK(!rec.has("error") && !rec.has("warning"));
    if (rec.has("warning")) std::cerr << "warning: " << rec.text_of("warning") << '\n';
    if (!contains(order, { "loadedmetadata", "canplay", "play", "pause", "ended" })) std::cerr << "events: " << joined(order) << '\n';
    grove::stop_media(doc);
}

void test_seeking_and_pixels() {
    grove::Project project = load("<body><video id=\"v\" src=\"clip.mp4\" /><canvas id=\"c\" /><video id=\"r\" src=\"rotated.mp4\" /><video id=\"w\" src=\"wide.mp4\" /></body>", "seek.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    grove::Node* r = doc.find_by_id("r");
    grove::Node* wide = doc.find_by_id("w");
    Recorder rec;
    rec.listen(doc, v);
    Recorder rrec;
    rrec.listen(doc, r);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("loadeddata") && rrec.has("loadeddata"); }));

    std::vector<std::uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    CHECK(grove::media_snapshot(doc, *v, rgba, w, h) && w == 160 && h == 96);
    CHECK(is_red(pixel_at(rgba, w, 40, 60)));
    CHECK(is_blue(pixel_at(rgba, w, 120, 60)));
    grove::Node* canvas = doc.find_by_id("c");
    CHECK(grove::media_snapshot_to_canvas(doc, *v, *canvas));
    const std::shared_ptr<grove::CanvasPixels> pixels = grove::canvas_of(doc, *canvas);
    CHECK(pixels->width == 160 && pixels->height == 96);
    CHECK(is_red(pixel_at(pixels->rgba, pixels->width, 20, 80)) && is_blue(pixel_at(pixels->rgba, pixels->width, 150, 80)));
    CHECK(!grove::media_snapshot_to_canvas(doc, *v, *v));

    grove::MediaFrame before;
    CHECK(grove::media_frame(doc, *v, before));
    const std::uint64_t serial = before.serial;
    CHECK(grove::media_seek(doc, *v, 1.0));
    CHECK(std::fabs(grove::media_position(doc, *v) - 1.0) < 0.02);
    CHECK(wait_until(doc, clock, [&] {
        grove::MediaFrame f;
        return grove::media_frame(doc, *v, f) && f.serial != serial;
    }));
    CHECK(contains(rec.events, { "seeking", "timeupdate", "seeked" }));
    CHECK(std::fabs(grove::media_position(doc, *v) - 1.0) < 0.02);
    CHECK(grove::media_seek(doc, *v, 99.0));
    const double end_pos = grove::media_position(doc, *v);
    CHECK(std::fabs(end_pos - 2.0) < 0.05);
    if (!(std::fabs(end_pos - 2.0) < 0.05)) std::cerr << "position after seeking past the end: " << end_pos << '\n';

    double rw = 0.0;
    double rh = 0.0;
    CHECK(grove::media_video_size(doc, *r, rw, rh) && rw == 64.0 && rh == 96.0);
    CHECK(grove::media_snapshot(doc, *r, rgba, w, h) && w == 64 && h == 96);
    CHECK(is_blue(pixel_at(rgba, w, 32, 10)));
    CHECK(is_red(pixel_at(rgba, w, 32, 86)));
    if (!(is_blue(pixel_at(rgba, w, 32, 10)) && is_red(pixel_at(rgba, w, 32, 86)))) {
        const Pixel a = pixel_at(rgba, w, 32, 10);
        const Pixel b = pixel_at(rgba, w, 32, 86);
        std::cerr << "rotated top " << a.r << ',' << a.g << ',' << a.b << " bottom " << b.r << ',' << b.g << ',' << b.b << '\n';
    }
    double ww = 0.0;
    double wh = 0.0;
    CHECK(wait_until(doc, clock, [&] { return grove::media_video_size(doc, *wide, ww, wh); }));
    CHECK(ww == 80.0 && wh == 48.0);
    grove::stop_media(doc);
}

void test_poster_preload_and_range() {
    grove::Project project = load(
        "<body><video id=\"p\" src=\"clip.mp4\" poster=\"poster.png\" preload=\"none\" />"
        "<video id=\"part\" src=\"clip.mp4\" start=\"0.5\" end=\"1.2\" /><video id=\"lp\" src=\"clip.mp4\" loop end=\"0.6\" /></body>",
        "poster.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* p = doc.find_by_id("p");
    grove::Node* part = doc.find_by_id("part");
    grove::Node* lp = doc.find_by_id("lp");
    Recorder rec;
    rec.listen(doc, p);
    Recorder rpart;
    rpart.listen(doc, part);
    Recorder rlp;
    rlp.listen(doc, lp);
    grove::start_media(doc);
    for (int i = 0; i < 20; ++i) pump(doc, clock, 10.0);
    CHECK(!rec.has("loadedmetadata"));
    CHECK(grove::media_poster(doc, *p) == (g_media / "poster.png").lexically_normal().string());
    grove::MediaFrame f;
    CHECK(!grove::media_frame(doc, *p, f));
    CHECK(grove::media_play(doc, *p));
    CHECK(play_until(doc, clock, [&] { return grove::media_frame(doc, *p, f) && grove::media_poster(doc, *p).empty(); }));
    CHECK(rec.has("loadedmetadata") && rec.has("play"));
    grove::media_pause(doc, *p);

    CHECK(wait_until(doc, clock, [&] { return rpart.has("loadeddata") && rlp.has("loadeddata"); }));
    CHECK(std::fabs(grove::media_position(doc, *part) - 0.5) < 0.02);
    CHECK(grove::media_play(doc, *part));
    CHECK(play_until(doc, clock, [&] { return rpart.has("ended"); }));
    CHECK(std::fabs(grove::media_position(doc, *part) - 1.2) < 0.02);
    CHECK(grove::media_play(doc, *lp));
    play_for(1.5, doc, clock);
    CHECK(grove::media_is_playing(doc, *lp) && !rlp.has("ended"));
    CHECK(grove::media_position(doc, *lp) <= 0.6 + 1e-6);
    grove::media_pause(doc, *lp);
    grove::stop_media(doc);
}

void test_unsupported_codec() {
    grove::Project project = load("<body><video id=\"v\" src=\"vp8.webm\" /></body>", "vp8.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    Recorder rec;
    rec.listen(doc, v);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("canplay") && rec.has("warning"); }));
    CHECK(rec.text_of("warning").find("vp8") != std::string::npos);
    std::cout << "  vp8 warning event: " << rec.text_of("warning") << '\n';
    CHECK(!rec.has("error"));
    CHECK(std::fabs(grove::media_duration(doc, *v) - 1.0) < 0.1);
    CHECK(grove::media_play(doc, *v));
    play_for(0.5, doc, clock);
    CHECK(grove::media_position(doc, *v) > 0.2);
    grove::MediaFrame f;
    CHECK(!grove::media_frame(doc, *v, f));
    CHECK(play_until(doc, clock, [&] { return rec.has("ended"); }));
    grove::stop_media(doc);
}

bool is_green(const Pixel& p) { return p.g > 90 && p.r < 60 && p.b < 60; }

void test_vp9_playback() {
    grove::Project project = load("<body><video id=\"v\" src=\"vp9.webm\" /><video id=\"i\" src=\"vp9.ivf\" /></body>", "vp9.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    grove::Node* ivf = doc.find_by_id("i");
    Recorder rec;
    rec.listen(doc, v);
    Recorder irec;
    irec.listen(doc, ivf);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("canplay") && rec.has("loadeddata") && irec.has("loadeddata"); }));
    CHECK(!rec.has("warning") && !rec.has("error"));
    if (rec.has("warning")) std::cerr << "vp9 warning: " << rec.text_of("warning") << '\n';
    CHECK(!irec.has("warning") && !irec.has("error"));
    CHECK(std::fabs(grove::media_duration(doc, *v) - 1.0) < 0.1);
    double vw = 0.0;
    double vh = 0.0;
    CHECK(grove::media_video_size(doc, *v, vw, vh) && vw == 64.0 && vh == 48.0);
    grove::MediaFrame f;
    CHECK(grove::media_frame(doc, *v, f) && f.rgba && f.width == 64 && f.height == 48 && f.serial > 0);
    if (f.rgba) CHECK(is_green(Pixel{ f.rgba[(6 * 64 + 8) * 4], f.rgba[(6 * 64 + 8) * 4 + 1], f.rgba[(6 * 64 + 8) * 4 + 2] }));
    std::vector<std::uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    CHECK(grove::media_snapshot(doc, *v, rgba, w, h) && w == 64 && h == 48 && is_green(pixel_at(rgba, w, 32, 24)));
    grove::MediaFrame fi;
    CHECK(grove::media_frame(doc, *ivf, fi) && fi.width == 64 && fi.height == 48);
    CHECK(grove::media_play(doc, *v));
    std::set<std::uint64_t> serials;
    play_for(0.6, doc, clock, [&] {
        grove::MediaFrame g;
        if (grove::media_frame(doc, *v, g)) serials.insert(g.serial);
    });
    CHECK(grove::media_position(doc, *v) > 0.3);
    CHECK(serials.size() >= 2);
    CHECK(play_until(doc, clock, [&] { return rec.has("ended"); }));
    CHECK(!rec.has("error"));
    grove::stop_media(doc);
}

void test_controls_and_fullscreen() {
    grove::Project project = load("<body><video id=\"v\" src=\"clip.mp4\" controls loop /><p id=\"other\">x</p></body>", "controls.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    Recorder rec;
    rec.listen(doc, v);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("loadeddata"); }));
    grove::Event click;
    click.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("v-play"), click);
    CHECK(grove::media_is_playing(doc, *v));
    grove::Node* row = doc.find_by_id("v-controls");
    CHECK(!row->has_class("idle"));
    play_for(3.0, doc, clock);
    CHECK(row->has_class("idle"));
    grove::media_pointer(doc, *v);
    CHECK(!row->has_class("idle"));
    grove::Event surface;
    surface.type = "click";
    surface.button = 1;
    doc.events().dispatch(doc, v, surface);
    CHECK(!grove::media_is_playing(doc, *v));
    play_for(3.0, doc, clock);
    CHECK(!row->has_class("idle"));

    grove::Event full;
    full.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("v-fullscreen"), full);
    CHECK(grove::media_is_fullscreen(*v) && v->has_class("fullscreen"));
    pump(doc, clock, 10.0);
    CHECK(doc.find_by_id("v-fullscreen")->has_class("active"));
    CHECK(rec.has("fullscreenchange"));
    grove::Event esc;
    esc.type = "key_down";
    esc.key = "Escape";
    doc.events().dispatch(doc, doc.find_by_id("other"), esc);
    CHECK(!grove::media_is_fullscreen(*v));
    grove::Event dbl;
    dbl.type = "double_click";
    dbl.button = 1;
    doc.events().dispatch(doc, v, dbl);
    CHECK(grove::media_is_fullscreen(*v));
    CHECK(grove::media_toggle_fullscreen(doc, *v) && !grove::media_is_fullscreen(*v));
    grove::Node* other = doc.find_by_id("other");
    CHECK(!grove::media_set_fullscreen(doc, *other, true));
    grove::stop_media(doc);
}

void test_c_api() {
    grove::Project project = load("<body><video id=\"x\" src=\"clip.mp4\" /><canvas id=\"c\" /><p id=\"p\">hi</p></body>", "capi.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove_document* d = reinterpret_cast<grove_document*>(&doc);
    grove_element* x = grove_find_by_id(d, "x");
    grove_element* c = grove_find_by_id(d, "c");
    grove_element* p = grove_find_by_id(d, "p");
    bool ready = false;
    doc.events().add_listener(doc.find_by_id("x"), "loadeddata", [&ready](grove::Event&) { ready = true; });
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return ready; }));
    CHECK(grove_video_width(d, x) == 160.0 && grove_video_height(d, x) == 96.0);
    CHECK(grove_video_width(d, p) == 0.0 && grove_video_width(nullptr, x) == 0.0);
    uint32_t w = 0;
    uint32_t h = 0;
    const size_t need = grove_video_snapshot(d, x, nullptr, 0, &w, &h);
    CHECK(need == 160u * 96u * 4u && w == 160 && h == 96);
    std::vector<std::uint8_t> buf(need);
    CHECK(grove_video_snapshot(d, x, buf.data(), buf.size(), nullptr, nullptr) == need);
    CHECK(is_red(pixel_at(buf, 160, 40, 60)));
    CHECK(grove_video_snapshot(d, p, buf.data(), buf.size(), &w, &h) == 0 && w == 0);
    CHECK(grove_video_snapshot_to_canvas(d, x, c));
    CHECK(!grove_video_snapshot_to_canvas(d, x, p));
    CHECK(grove_video_set_fullscreen(d, x, true) && grove_video_is_fullscreen(d, x));
    CHECK(grove_video_set_fullscreen(d, x, false) && !grove_video_is_fullscreen(d, x));
    CHECK(!grove_video_set_fullscreen(d, p, true));
    CHECK(grove_media_play(d, x) && grove_media_is_playing(d, x));
    play_for(0.3, doc, clock);
    CHECK(grove_media_position(d, x) > 0.1);
    CHECK(grove_media_pause(d, x) && !grove_media_is_playing(d, x));
    grove::stop_media(doc);
}

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void test_bundle_playback() {
    const std::string root = (g_dir / "bundle").string();
    std::map<std::string, std::string> files;
    files[grove::virtual_file_key(root + "/movie.mp4")] = read_bytes(g_media / "clip.mp4");
    files[grove::virtual_file_key(root + "/still.png")] = read_bytes(g_media / "poster.png");
    files[grove::virtual_file_key(root + "/song.m4a")] = read_bytes(g_media / "tone.m4a");
    grove::set_virtual_files(files);
    grove::Project project = grove::load_project_from_source("<body><video id=\"v\" src=\"movie.mp4\" poster=\"still.png\" /><audio id=\"a\" src=\"song.m4a\" /></body>", root + "/app.grove", root);
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* v = doc.find_by_id("v");
    grove::Node* a = doc.find_by_id("a");
    CHECK(grove::media_problems(doc, *v).empty());
    Recorder rec;
    rec.listen(doc, v);
    Recorder arec;
    arec.listen(doc, a);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("loadeddata") && arec.has("canplay"); }));
    CHECK(!rec.has("error") && !arec.has("error"));
    CHECK(std::fabs(grove::media_duration(doc, *v) - 2.0) < 0.05);
    CHECK(grove::media_play(doc, *v) && grove::media_play(doc, *a));
    std::set<std::uint64_t> serials;
    play_for(0.6, doc, clock, [&] {
        grove::MediaFrame f;
        if (grove::media_frame(doc, *v, f)) serials.insert(f.serial);
    });
    CHECK(serials.size() >= 3);
    CHECK(grove::media_position(doc, *v) > 0.3);
    CHECK(grove::media_position(doc, *a) > 0.3);
    std::vector<std::uint8_t> rgba;
    unsigned w = 0;
    unsigned h = 0;
    CHECK(grove::media_snapshot(doc, *v, rgba, w, h) && is_blue(pixel_at(rgba, w, 120, 60)));
    grove::stop_media(doc);
    grove::set_virtual_files({});

    std::set<std::string> read;
    grove::set_file_read_observer([&read](const std::string& path, const std::string&) { read.insert(fs::path(path).filename().string()); });
    grove::Project checked = load("<body><video id=\"v\" src=\"clip.mp4\" poster=\"poster.png\" /></body>", "observe.grove");
    grove::set_file_read_observer(nullptr);
    CHECK(read.count("clip.mp4") == 1 && read.count("poster.png") == 1);
}

void test_audio_containers() {
    grove::Project project = load(
        "<body><audio id=\"m4a\" src=\"tone.m4a\" /><audio id=\"webm\" src=\"tone.webm\" /><audio id=\"opus\" src=\"tone.opus\" /><audio id=\"aac\" src=\"tone.aac\" /></body>",
        "containers.grove");
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    std::vector<std::pair<std::string, Recorder>> recs(4);
    const char* ids[] = { "m4a", "webm", "opus", "aac" };
    for (int i = 0; i < 4; ++i) {
        recs[static_cast<std::size_t>(i)].first = ids[i];
        recs[static_cast<std::size_t>(i)].second.listen(doc, doc.find_by_id(ids[i]));
        CHECK(grove::media_problems(doc, *doc.find_by_id(ids[i])).empty());
    }
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] {
        for (auto& r : recs) {
            if (!r.second.has("canplay") && !r.second.has("error")) return false;
        }
        return true;
    }));
    for (auto& r : recs) {
        if (!r.second.has("canplay")) std::cerr << r.first << ": no canplay; events " << joined(r.second.events) << " error " << r.second.text_of("error") << '\n';
    }
    for (auto& r : recs) {
        grove::Node* n = doc.find_by_id(r.first);
        const double d = grove::media_duration(doc, *n);
        CHECK(!r.second.has("error"));
        CHECK(d > 0.9 && d < 1.1);
        if (r.second.has("error") || !(d > 0.9 && d < 1.1)) std::cerr << r.first << ": duration " << d << " error " << r.second.text_of("error") << '\n';
        CHECK(grove::media_play(doc, *n));
    }
    play_for(0.3, doc, clock);
    for (auto& r : recs) CHECK(grove::media_level(doc, *doc.find_by_id(r.first)) > 0.05);
    CHECK(play_until(doc, clock, [&] {
        for (auto& r : recs) {
            if (!r.second.has("ended")) return false;
        }
        return true;
    }));
    grove::stop_media(doc);
}

} // namespace

int main() {
    g_media = fs::path(GROVE_VIDEO_TEST_DIR);
    g_dir = fs::temp_directory_path() / ("grove-video-tests-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007)));
    fs::create_directories(g_dir);
    std::string error;
    if (!fizmo::audio::set_backend("null", &error)) std::cerr << "can't use the null audio backend: " << error << '\n';
    grove::audio::set_manual_output(true);
    test_parsing_and_controls();
    test_check_warnings();
    test_metadata_events_and_frames();
    test_planar_frames();
    test_seeking_and_pixels();
    test_poster_preload_and_range();
    test_unsupported_codec();
    test_vp9_playback();
    test_controls_and_fullscreen();
    test_c_api();
    test_bundle_playback();
    test_audio_containers();
    grove::audio::shutdown();
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
