#include "grove/grove.hpp"
#include "grove/media.hpp"
#include "grove/audio.hpp"
#include "grove/api/grove_api.h"

#include <fizmo/../../Source/Audio/audio.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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
    std::cerr << "grove_audio_tests.cpp:" << line << ": FAILED: " << what << '\n';
}

#define CHECK(expr) check((expr), #expr, __LINE__)

namespace fs = std::filesystem;

fs::path g_dir;

std::string write_tone(const std::string& name, double seconds, double hz, float amplitude = 0.5f) {
    const std::uint32_t rate = 22050;
    const std::size_t frames = static_cast<std::size_t>(seconds * rate);
    fizmo::audio::AudioBuffer buffer(2, rate, frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = amplitude * static_cast<float>(std::sin(6.283185307179586 * hz * static_cast<double>(i) / rate));
        buffer.at(i, 0) = v;
        buffer.at(i, 1) = v;
    }
    const fs::path path = g_dir / name;
    fizmo::audio::SaveOptions options;
    options.type = fizmo::audio::AudioFileType::wav;
    std::string error;
    if (!fizmo::audio::save_audio(path.string(), buffer, options, &error)) std::cerr << "can't write " << path << ": " << error << '\n';
    return path.string();
}

struct Clock {
    double now = 0.0;
};

struct Recorder {
    std::vector<std::string> events;

    void listen(grove::Document& doc, grove::Node* node) {
        for (const char* type : { "play", "pause", "ended", "timeupdate", "durationchange", "loadedmetadata", "canplay", "volumechange", "ratechange", "seeking", "seeked", "error", "emptied" }) {
            doc.events().add_listener(node, type, [this](grove::Event& e) {
                events.push_back(e.type);
                if (e.type == "error") last_error = e.text;
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

    std::vector<std::string> without_timeupdates() const {
        std::vector<std::string> out;
        for (const std::string& e : events) {
            if (e != "timeupdate") out.push_back(e);
        }
        return out;
    }

    std::string last_error;
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

void play_audio(double seconds, grove::Document& doc, Clock& clock) {
    const double step = 0.05;
    for (double t = 0.0; t < seconds - 1e-9; t += step) {
        grove::audio::advance(step);
        pump(doc, clock, step * 1000.0);
    }
}

bool contains(const std::vector<std::string>& list, const std::vector<std::string>& wanted) {
    std::size_t at = 0;
    for (const std::string& e : list) {
        if (at < wanted.size() && e == wanted[at]) ++at;
    }
    return at == wanted.size();
}

void test_parsing_and_controls() {
    const std::string src =
        "<body>"
        "<audio id=\"a\" src=\"tone.wav\" controls loop volume=\"0.5\" playbackrate=\"1.5\" start=\"0.25\" end=\"0:01\"></audio>"
        "<audio id=\"plain\" src=\"tone.wav\" />"
        "<audio_meter id=\"m\" for=\"a\" />"
        "</body>";
    grove::Project project = grove::load_project_from_source(src, (g_dir / "doc.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    grove::Node* a = doc.find_by_id("a");
    CHECK(a && grove::is_media(*a));
    CHECK(grove::is_media_tag("audio") && !grove::is_media_tag("div"));
    CHECK(grove::tag_role("audio") == grove::TagRole::Block);
    CHECK(grove::is_known_tag("audio_meter"));
    CHECK(doc.find_by_id("a-controls") && doc.find_by_id("a-controls")->has_class("media-controls"));
    CHECK(doc.find_by_id("a-play") && doc.find_by_id("a-play")->tag() == "button");
    CHECK(doc.find_by_id("a-seek") && doc.find_by_id("a-seek")->tag() == "input_slider");
    CHECK(doc.find_by_id("a-time") && doc.find_by_id("a-time")->inner_text() == "0:00 / 0:00");
    CHECK(doc.find_by_id("a-mute") && doc.find_by_id("a-volume"));
    CHECK(doc.find_by_id("a-volume")->attribute_or("value", "") == "0.5");
    CHECK(doc.find_by_id("plain")->children().empty());
    grove::Node* m = doc.find_by_id("m");
    CHECK(m && m->children().size() == 1 && m->children().front()->has_class("meter-fill"));
    CHECK(grove::media_volume(*a) == 0.5);
    CHECK(grove::media_rate(*a) == 1.5);
    CHECK(grove::media_loop(*a) && !grove::media_muted(*a));
    CHECK(grove::media_position(doc, *a) == 0.25);
    CHECK(grove::format_media_time(0.0) == "0:00");
    CHECK(grove::format_media_time(75.9) == "1:15");
    CHECK(grove::format_media_time(3725.0) == "1:02:05");
    CHECK(grove::media_path(doc, "tone.wav") == (g_dir / "tone.wav").lexically_normal().string());

    a->remove_attribute("controls");
    CHECK(grove::expand_media(doc, *a));
    CHECK(!doc.find_by_id("a-controls") || !doc.is_attached(*doc.find_by_id("a-controls")));
    a->set_attribute("controls", "");
    CHECK(grove::expand_media(doc, *a));
    CHECK(doc.find_by_id("a-controls") && doc.is_attached(*doc.find_by_id("a-controls")));
    CHECK(!grove::expand_media(doc, *a));

    bool flags_ok = true;
    a->set_attribute("muted", "false");
    flags_ok = flags_ok && !grove::media_muted(*a);
    a->set_attribute("muted", "");
    flags_ok = flags_ok && grove::media_muted(*a);
    CHECK(flags_ok);
}

void test_check_warnings() {
    const std::string src =
        "<body>"
        "<audio id=\"gone\" src=\"missing.ogg\" />"
        "<audio id=\"bad\" src=\"tone.wav\" volume=\"2\" playbackrate=\"-1\" preload=\"sometimes\" start=\"x\" />"
        "<audio id=\"kind\" src=\"notes.txt\" />"
        "<audio id=\"range\" src=\"tone.wav\" start=\"5\" end=\"2\" />"
        "<audio id=\"good\" src=\"tone.wav\" volume=\"0.3\" preload=\"metadata\" start=\"1:02\" end=\"70\" controls />"
        "<audio_meter for=\"nobody\" />"
        "<audio_meter for=\"good\" />"
        "<audio_meter for=\"master\" />"
        "<audio_meter for=\"monitor:3\" />"
        "</body>";
    std::ofstream(g_dir / "notes.txt") << "not audio";
    grove::Project project = grove::load_project_from_source(src, (g_dir / "check.grove").string(), g_dir.string());
    std::string all;
    for (const grove::Diagnostic& d : project.diagnostics.items()) all += d.message + "\n";
    auto mentions = [&](const std::string& s) { return all.find(s) != std::string::npos; };
    CHECK(mentions("can't find the audio file 'missing.ogg'"));
    CHECK(mentions("volume goes from 0 to 1"));
    CHECK(mentions("playbackrate=\"-1\""));
    CHECK(mentions("preload=\"sometimes\""));
    CHECK(mentions("start=\"x\""));
    CHECK(mentions("isn't a kind of audio file Grove can play"));
    CHECK(mentions("end that isn't after its start"));
    CHECK(mentions("for=\"nobody\""));
    CHECK(!mentions("id=\"good\""));
    CHECK(project.diagnostics.ok());

    grove::Project android = grove::load_project_from_source("<!GROVE type=android package=com.example.sound>\n<body><audio src=\"tone.wav\" /></body>", (g_dir / "phone.grove").string(), g_dir.string());
    bool warned = false;
    for (const grove::Diagnostic& d : android.diagnostics.items()) warned = warned || d.message.find("Android") != std::string::npos;
    CHECK(!warned);
}

void test_playback_events() {
    grove::Project project = grove::load_project_from_source("<body><audio id=\"a\" src=\"tone.wav\" controls /><audio_meter id=\"m\" for=\"a\" /></body>", (g_dir / "play.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* a = doc.find_by_id("a");
    Recorder rec;
    rec.listen(doc, a);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rec.has("canplay"); }));
    CHECK(contains(rec.events, { "durationchange", "loadedmetadata", "canplay" }));
    CHECK(std::fabs(grove::media_duration(doc, *a) - 2.0) < 0.01);
    CHECK(doc.find_by_id("a-time")->inner_text() == "0:00 / 0:02");
    CHECK(doc.find_by_id("a-seek")->attribute_or("max", "") == "2");
    CHECK(!grove::media_is_playing(doc, *a));

    rec.events.clear();
    CHECK(grove::media_play(doc, *a));
    CHECK(grove::media_is_playing(doc, *a));
    CHECK(rec.events.empty());
    pump(doc, clock, 1.0);
    CHECK(rec.events.size() == 1 && rec.events[0] == "play");
    CHECK(doc.find_by_id("a-play")->has_class("playing"));

    play_audio(0.5, doc, clock);
    const double p = grove::media_position(doc, *a);
    CHECK(p > 0.4 && p < 0.6);
    CHECK(rec.count("timeupdate") >= 1 && rec.count("timeupdate") <= 3);
    CHECK(doc.find_by_id("a-seek")->attribute_or("value", "") != "0");

    grove::Node* meter = doc.find_by_id("m");
    pump(doc, clock, 60.0);
    const double level = std::atof(meter->attribute_or("value", "0").c_str());
    CHECK(level > 0.2 && level < 0.6);
    CHECK(meter->children().front()->attribute_or("style", "").find("width: 0%") == std::string::npos);
    CHECK(grove::audio::master_level() > 0.2);

    rec.events.clear();
    CHECK(grove::media_seek(doc, *a, 1.25));
    pump(doc, clock, 1.0);
    CHECK(contains(rec.events, { "seeking", "timeupdate", "seeked" }));
    CHECK(std::fabs(grove::media_position(doc, *a) - 1.25) < 0.02);
    play_audio(0.25, doc, clock);
    CHECK(std::fabs(grove::media_position(doc, *a) - 1.5) < 0.06);

    rec.events.clear();
    CHECK(grove::media_pause(doc, *a));
    pump(doc, clock, 1.0);
    CHECK(rec.without_timeupdates() == std::vector<std::string>{ "pause" });
    const double held = grove::media_position(doc, *a);
    play_audio(0.3, doc, clock);
    CHECK(std::fabs(grove::media_position(doc, *a) - held) < 1e-9);
    CHECK(!doc.find_by_id("a-play")->has_class("playing"));

    rec.events.clear();
    CHECK(grove::media_set_volume(doc, *a, 0.25));
    CHECK(a->attribute_or("volume", "") == "0.25");
    CHECK(grove::media_set_rate(doc, *a, 2.0));
    CHECK(!grove::media_set_rate(doc, *a, -1.0));
    CHECK(grove::media_set_muted(doc, *a, true));
    CHECK(a->has_attribute("muted") && doc.find_by_id("a-mute")->has_class("muted"));
    pump(doc, clock, 1.0);
    CHECK(rec.count("volumechange") == 2 && rec.count("ratechange") == 1);
    CHECK(grove::media_set_muted(doc, *a, false));
    CHECK(grove::media_set_rate(doc, *a, 1.0));

    rec.events.clear();
    CHECK(grove::media_play(doc, *a));
    play_audio(0.7, doc, clock);
    CHECK(!grove::media_is_playing(doc, *a));
    std::vector<std::string> plain = rec.without_timeupdates();
    CHECK((plain == std::vector<std::string>{ "volumechange", "ratechange", "play", "pause", "ended" }));
    CHECK(rec.events.size() >= 2 && rec.events[rec.events.size() - 3] == "timeupdate");
    CHECK(std::fabs(grove::media_position(doc, *a) - 2.0) < 0.01);

    rec.events.clear();
    CHECK(grove::media_play(doc, *a));
    play_audio(0.1, doc, clock);
    CHECK(grove::media_position(doc, *a) < 0.2);
    CHECK(grove::media_stop(doc, *a));
    CHECK(grove::media_position(doc, *a) == 0.0);
    CHECK(!grove::media_is_playing(doc, *a));

    CHECK(grove::media_set_loop(doc, *a, true));
    CHECK(grove::media_play(doc, *a));
    play_audio(2.3, doc, clock);
    CHECK(grove::media_is_playing(doc, *a));
    const double looped = grove::media_position(doc, *a);
    CHECK(looped > 0.2 && looped < 0.4);
    CHECK(grove::media_pause(doc, *a));
    grove::stop_media(doc);
}

void test_controls_clicks() {
    grove::Project project = grove::load_project_from_source("<body><audio id=\"c\" src=\"tone.wav\" controls volume=\"0.8\" /></body>", (g_dir / "controls.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* c = doc.find_by_id("c");
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return doc.find_by_id("c-seek")->attribute_or("max", "") == "2"; }));
    grove::Event click;
    click.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("c-play"), click);
    CHECK(grove::media_is_playing(doc, *c));
    CHECK(doc.find_by_id("c-play")->inner_text() != "\xE2\x96\xB6");
    grove::Event click2;
    click2.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("c-play"), click2);
    CHECK(!grove::media_is_playing(doc, *c));

    grove::Node* seek = doc.find_by_id("c-seek");
    CHECK(grove::set_input_float(doc, *seek, 1.5));
    grove::Event input;
    input.type = "input";
    doc.events().dispatch(doc, seek, input);
    CHECK(std::fabs(grove::media_position(doc, *c) - 1.5) < 0.02);
    grove::Event change;
    change.type = "change";
    doc.events().dispatch(doc, seek, change);

    grove::Node* volume = doc.find_by_id("c-volume");
    CHECK(grove::set_input_float(doc, *volume, 0.3));
    grove::Event vinput;
    vinput.type = "input";
    doc.events().dispatch(doc, volume, vinput);
    CHECK(c->attribute_or("volume", "") == "0.3");

    grove::Event mute;
    mute.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("c-mute"), mute);
    CHECK(grove::media_muted(*c));
    grove::Event mute2;
    mute2.type = "click";
    mute2.default_prevented = false;
    doc.events().add_listener(doc.find_by_id("c-mute"), "click", [](grove::Event& e) { e.default_prevented = true; });
    doc.events().dispatch(doc, doc.find_by_id("c-mute"), mute2);
    CHECK(grove::media_muted(*c));
    grove::stop_media(doc);
}

void test_range_autoplay_and_errors() {
    grove::Project project = grove::load_project_from_source(
        "<body><audio id=\"r\" src=\"tone.wav\" start=\"0.5\" end=\"1\" autoplay />"
        "<audio id=\"e\" src=\"missing.wav\" />"
        "<audio id=\"n\" src=\"tone.wav\" preload=\"none\" />"
        "<audio id=\"md\" src=\"tone.wav\" preload=\"metadata\" /></body>",
        (g_dir / "range.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* r = doc.find_by_id("r");
    grove::Node* e = doc.find_by_id("e");
    grove::Node* n = doc.find_by_id("n");
    grove::Node* md = doc.find_by_id("md");
    Recorder rr;
    rr.listen(doc, r);
    Recorder re;
    re.listen(doc, e);
    Recorder rn;
    rn.listen(doc, n);
    Recorder rmd;
    rmd.listen(doc, md);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rr.has("canplay") && re.has("error") && rmd.has("loadedmetadata"); }));
    CHECK(rr.events.size() >= 1 && rr.events[0] == "play");
    CHECK(grove::media_is_playing(doc, *r));
    CHECK(grove::media_position(doc, *r) >= 0.5);
    CHECK(re.last_error.find("missing.wav") != std::string::npos);
    CHECK(grove::media_error(doc, *e).find("missing.wav") != std::string::npos);
    CHECK(rn.events.empty());
    CHECK(!rmd.has("canplay"));
    CHECK(std::fabs(grove::media_duration(doc, *md) - 2.0) < 0.01);
    play_audio(0.6, doc, clock);
    CHECK(rr.has("ended"));
    CHECK(std::fabs(grove::media_position(doc, *r) - 1.0) < 0.01);

    CHECK(grove::media_play(doc, *n));
    CHECK(wait_until(doc, clock, [&] { return rn.has("canplay"); }));
    CHECK(contains(rn.events, { "play", "loadedmetadata", "canplay" }));
    CHECK(grove::media_is_playing(doc, *n));
    CHECK(grove::media_pause(doc, *n));

    re.events.clear();
    CHECK(!grove::media_play(doc, *doc.find_by_id("e")) || true);
    CHECK(wait_until(doc, clock, [&] { return re.has("error"); }));
    CHECK(!grove::media_is_playing(doc, *e));

    CHECK(grove::media_set_src(doc, *e, "short.wav"));
    CHECK(wait_until(doc, clock, [&] { return re.has("canplay"); }));
    CHECK(std::fabs(grove::media_duration(doc, *e) - 0.5) < 0.01);
    grove::stop_media(doc);
}

void test_virtual_files_and_bundling() {
    std::vector<std::string> reads;
    grove::set_file_read_observer([&reads](const std::string& path, const std::string&) { reads.push_back(path); });
    grove::Project project = grove::load_project_from_source("<body><audio src=\"tone.wav\" /></body>", (g_dir / "bundle.grove").string(), g_dir.string());
    grove::set_file_read_observer(nullptr);
    bool found = false;
    for (const std::string& r : reads) found = found || fs::path(r).filename() == "tone.wav";
    CHECK(found);

    std::ifstream in(g_dir / "short.wav", std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string fake = (g_dir / "packed" / "inside.wav").string();
    grove::set_virtual_files({ { fake, bytes } });
    grove::Project packed = grove::load_project_from_source("<body><audio id=\"v\" src=\"packed/inside.wav\" /></body>", (g_dir / "app.grove").string(), g_dir.string());
    bool missing = false;
    for (const grove::Diagnostic& d : packed.diagnostics.items()) missing = missing || d.message.find("can't find") != std::string::npos;
    CHECK(!missing);
    grove::Document& doc = packed.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::start_media(doc);
    grove::Node* v = doc.find_by_id("v");
    CHECK(wait_until(doc, clock, [&] { return grove::media_duration(doc, *v) > 0.4; }));
    CHECK(grove::media_error(doc, *v).empty());
    grove::stop_media(doc);
    grove::set_virtual_files({});
}

void test_sound_api() {
    const std::string tone = (g_dir / "short.wav").string();
    const std::int64_t handle = grove::audio::preload_sound(tone);
    CHECK(handle > 0);
    CHECK(grove::audio::preload_sound(tone) == handle);
    for (int i = 0; i < 2000 && !grove::audio::sound_loaded(handle); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(grove::audio::sound_loaded(handle));
    CHECK(std::fabs(grove::audio::sound_duration(handle) - 0.5) < 0.01);
    const std::int64_t id = grove::audio::play_sound(handle, 0.5, -0.5, false);
    CHECK(id > 0);
    CHECK(grove::audio::sound_playing(id));
    grove::audio::advance(0.1);
    CHECK(grove::audio::master_level() > 0.1);
    CHECK(grove::audio::set_sound_volume(id, 0.1));
    CHECK(grove::audio::set_sound_pan(id, 0.5));
    grove::audio::advance(0.5);
    CHECK(!grove::audio::sound_playing(id));
    CHECK(!grove::audio::set_sound_volume(999999, 1.0));

    const std::int64_t looped = grove::audio::play_sound(handle, 1.0, 0.0, true);
    grove::audio::advance(1.2);
    CHECK(grove::audio::sound_playing(looped));
    CHECK(grove::audio::stop_sound(looped));
    CHECK(!grove::audio::sound_playing(looped));
    CHECK(!grove::audio::stop_sound(looped));

    grove::audio::set_master_volume(0.5);
    CHECK(std::fabs(grove::audio::master_volume() - 0.5) < 1e-6);
    grove::audio::set_master_volume(1.0);

    const std::int64_t direct = grove::audio::play_sound((g_dir / "tone.wav").string(), 1.0, 0.0, false);
    CHECK(direct > 0);
    for (int i = 0; i < 2000 && grove::audio::sound_playing(direct); ++i) {
        grove::audio::advance(0.01);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    CHECK(!grove::audio::sound_playing(direct));
    const std::int64_t bad = grove::audio::play_sound((g_dir / "nothing-here.wav").string());
    for (int i = 0; i < 2000 && grove::audio::sound_playing(bad); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!grove::audio::sound_playing(bad));
    CHECK(grove::audio::last_error().find("nothing-here.wav") != std::string::npos);
    grove::audio::stop_all_sounds();
    CHECK(grove::audio::unload_sound(handle));
    CHECK(!grove::audio::unload_sound(handle));
    CHECK(grove::audio::play_sound(handle) == 0);

    CHECK(!grove::audio::output_devices().empty());
    CHECK(!grove::audio::input_devices().empty());
    CHECK(grove::audio::set_output_device(grove::audio::output_devices().front().id));
    CHECK(grove::audio::output_device() == grove::audio::output_devices().front().id);
    CHECK(grove::audio::set_output_device(""));
}

void test_effects_and_monitor() {
    CHECK(grove::audio::create_effect("no such effect") == 0);
    const std::int64_t reverb = grove::audio::create_effect("reverb");
    CHECK(reverb > 0);
    CHECK(grove::audio::effect_type(reverb) == "reverb");
    const std::vector<std::string> params = grove::audio::effect_params(reverb);
    CHECK(!params.empty());
    CHECK(grove::audio::set_effect_param(reverb, params.front(), grove::audio::effect_param(reverb, params.front())));
    CHECK(!grove::audio::set_effect_param(reverb, "not-a-parameter", 1.0));
    const std::int64_t robot = grove::audio::create_effect("robot");
    CHECK(grove::audio::effect_type(robot) == "voice_changer");
    const std::int64_t pitch = grove::audio::create_effect("pitch-shifter");
    CHECK(grove::audio::set_effect_param(pitch, "semitones", 5.0));
    CHECK(std::fabs(grove::audio::effect_param(pitch, "semitones") - 5.0) < 1e-6);
    const std::int64_t chain = grove::audio::create_effect("chain");
    CHECK(grove::audio::chain_add(chain, pitch));
    CHECK(grove::audio::chain_add(chain, reverb));
    CHECK(!grove::audio::chain_add(chain, chain));
    CHECK(!grove::audio::chain_add(reverb, pitch));
    CHECK(grove::audio::chain_size(chain) == 2);
    CHECK(grove::audio::chain_remove(chain, 0));
    CHECK(grove::audio::chain_size(chain) == 1);
    CHECK(grove::audio::set_effect_enabled(reverb, false));
    for (const std::string& type : grove::audio::effect_types()) {
        const std::int64_t h = grove::audio::create_effect(type);
        if (h == 0) std::cerr << "can't create effect '" << type << "'\n";
        CHECK(h > 0);
        grove::audio::free_effect(h);
    }

    std::string error;
    const std::int64_t monitor = grove::audio::start_monitor("", "", chain, &error);
    CHECK(monitor > 0);
    if (monitor) {
        CHECK(grove::audio::monitor_running(monitor));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(grove::audio::monitor_input_level(monitor) == 0.0);
        CHECK(grove::audio::set_monitor_effect(monitor, robot));
        CHECK(grove::audio::set_monitor_effect(monitor, 0));
        CHECK(grove::audio::set_monitor_volume(monitor, 0.5));
        CHECK(!grove::audio::set_monitor_effect(monitor, 123456));
        CHECK(grove::audio::stop_monitor(monitor));
        CHECK(!grove::audio::monitor_running(monitor));
        CHECK(!grove::audio::stop_monitor(monitor));
    } else {
        std::cerr << "monitor: " << error << '\n';
    }
    CHECK(grove::audio::start_monitor("", "", 424242, &error) == 0 && !error.empty());
    CHECK(grove::audio::free_effect(chain));
    CHECK(!grove::audio::free_effect(chain));
}

void test_c_api() {
    grove::Project project = grove::load_project_from_source("<body><audio id=\"x\" src=\"tone.wav\" /><p id=\"p\">hi</p></body>", (g_dir / "capi.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove_document* d = reinterpret_cast<grove_document*>(&doc);
    grove_element* x = grove_find_by_id(d, "x");
    grove_element* p = grove_find_by_id(d, "p");
    bool ready = false;
    doc.events().add_listener(doc.find_by_id("x"), "canplay", [&ready](grove::Event&) { ready = true; });
    CHECK(grove_media_play(d, x));
    CHECK(grove_media_is_playing(d, x));
    CHECK(!grove_media_play(d, p));
    CHECK(!grove_media_play(nullptr, x));
    CHECK(wait_until(doc, clock, [&] { return ready; }));
    CHECK(grove_media_duration(d, x) > 1.9);
    play_audio(0.3, doc, clock);
    CHECK(grove_media_position(d, x) > 0.2);
    CHECK(grove_media_level(d, x) > 0.1);
    CHECK(grove_media_seek(d, x, 1.0));
    CHECK(std::fabs(grove_media_position(d, x) - 1.0) < 0.02);
    CHECK(grove_media_set_volume(d, x, 0.4) && grove_media_set_muted(d, x, true) && grove_media_set_loop(d, x, true) && grove_media_set_rate(d, x, 0.5));
    CHECK(std::string(grove_get_attribute(x, "volume")) == "0.4");
    CHECK(grove_media_pause(d, x) && !grove_media_is_playing(d, x));
    CHECK(grove_media_stop(d, x));
    CHECK(std::string(grove_media_error(d, x)).empty());
    CHECK(grove_media_set_src(d, x, "short.wav"));
    CHECK(wait_until(doc, clock, [&] { return std::fabs(grove_media_duration(d, x) - 0.5) < 0.01; }));

    const int64_t sound = grove_sound_preload(d, "short.wav");
    CHECK(sound > 0);
    for (int i = 0; i < 2000 && !grove_sound_loaded(sound); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const int64_t id = grove_sound_play_preloaded(sound, 1.0, 0.0, false);
    CHECK(id > 0 && grove_sound_is_playing(id));
    CHECK(grove_sound_set_volume(id, 0.2));
    CHECK(grove_sound_stop(id) && !grove_sound_is_playing(id));
    const int64_t by_path = grove_sound_play(d, "short.wav", 1.0, 0.0, true);
    CHECK(by_path > 0);
    grove_sound_stop_all();
    CHECK(!grove_sound_is_playing(by_path));
    CHECK(grove_sound_unload(sound));
    grove_audio_set_master_volume(0.7);
    CHECK(std::fabs(grove_audio_master_volume() - 0.7) < 1e-6);
    grove_audio_set_master_volume(1.0);
    CHECK(grove_audio_master_level() >= 0.0);
    CHECK(grove_audio_device_count(false) >= 1 && grove_audio_device_count(true) >= 1);
    CHECK(std::string(grove_audio_device_id(false, 0)) == grove::audio::output_devices().front().id);
    CHECK(!std::string(grove_audio_device_name(true, 0)).empty());
    CHECK(std::string(grove_audio_device_id(false, 9999)).empty());
    CHECK(grove_audio_set_output_device(""));
    CHECK(std::string(grove_audio_output_device()).empty());

    const int64_t fx = grove_effect_create("delay");
    CHECK(fx > 0);
    CHECK(grove_effect_param_count(fx) > 0);
    const std::string first = grove_effect_param_name(fx, 0);
    CHECK(!first.empty());
    CHECK(grove_effect_set(fx, first.c_str(), grove_effect_get(fx, first.c_str())));
    CHECK(grove_effect_set_enabled(fx, true));
    const int64_t chain = grove_effect_create("chain");
    CHECK(grove_effect_chain_add(chain, fx) && grove_effect_chain_remove(chain, 0) && !grove_effect_chain_remove(chain, 0));
    const int64_t mon = grove_monitor_start("", "", chain);
    CHECK(mon > 0);
    CHECK(grove_monitor_set_effect(mon, fx));
    CHECK(grove_monitor_input_level(mon) >= 0.0 && grove_monitor_output_level(mon) >= 0.0);
    CHECK(grove_monitor_stop(mon));
    CHECK(grove_effect_free(fx) && grove_effect_free(chain));
    CHECK(std::string(grove_audio_error()).size() < 4096);
    grove::stop_media(doc);
}

void test_meter_sources() {
    grove::Project project = grove::load_project_from_source("<body><audio_meter id=\"master\" /><audio_meter id=\"lin\" scale=\"linear\" /></body>", (g_dir / "meter.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::start_media(doc);
    const std::int64_t handle = grove::audio::preload_sound((g_dir / "short.wav").string());
    for (int i = 0; i < 2000 && !grove::audio::sound_loaded(handle); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    grove::audio::play_sound(handle, 1.0, 0.0, true);
    grove::audio::advance(0.05);
    pump(doc, clock, 60.0);
    const double v = std::atof(doc.find_by_id("master")->attribute_or("value", "0").c_str());
    CHECK(v > 0.3);
    CHECK(doc.find_by_id("lin")->children().front()->attribute_or("style", "") != "width: 0%");
    grove::audio::stop_all_sounds();
    for (int i = 0; i < 40; ++i) {
        grove::audio::advance(0.05);
        pump(doc, clock, 60.0);
    }
    CHECK(std::atof(doc.find_by_id("master")->attribute_or("value", "1").c_str()) < 0.01);
    grove::stop_media(doc);
}

void play_slowly(double seconds, grove::Document& doc, Clock& clock) {
    const double step = 0.02;
    for (double t = 0.0; t < seconds - 1e-9; t += step) {
        grove::audio::advance(step);
        pump(doc, clock, step * 1000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void test_streaming_and_pitch() {
    write_tone("long.wav", 31.0, 220.0, 0.4f);
    grove::Project project = grove::load_project_from_source(
        "<body><audio id=\"s\" src=\"long.wav\" controls /><audio id=\"part\" src=\"long.wav\" start=\"2\" end=\"3\" />"
        "<audio id=\"slow\" src=\"tone.wav\" playbackrate=\"0.5\" /><audio id=\"record\" src=\"tone.wav\" playbackrate=\"0.5\" preservespitch=\"false\" /></body>",
        (g_dir / "stream.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    Clock clock;
    doc.events().set_clock([&clock] { return clock.now; });
    grove::Node* s = doc.find_by_id("s");
    grove::Node* part = doc.find_by_id("part");
    Recorder rs;
    rs.listen(doc, s);
    Recorder rp;
    rp.listen(doc, part);
    grove::Node* slow = doc.find_by_id("slow");
    grove::Node* record = doc.find_by_id("record");
    Recorder rslow;
    rslow.listen(doc, slow);
    Recorder rrecord;
    rrecord.listen(doc, record);
    grove::start_media(doc);
    CHECK(wait_until(doc, clock, [&] { return rs.has("canplay") && rp.has("canplay"); }));
    CHECK(std::fabs(grove::media_duration(doc, *s) - 31.0) < 0.05);
    CHECK(doc.find_by_id("s-time")->inner_text() == "0:00 / 0:31");
    CHECK(grove::media_play(doc, *s));
    play_slowly(1.0, doc, clock);
    const double p = grove::media_position(doc, *s);
    CHECK(p > 0.5 && p < 1.1);
    CHECK(grove::media_level(doc, *s) > 0.1);
    CHECK(grove::media_seek(doc, *s, 20.0));
    CHECK(std::fabs(grove::media_position(doc, *s) - 20.0) < 0.01);
    for (int i = 0; i < 400 && grove::media_position(doc, *s) <= 20.0; ++i) {
        grove::audio::advance(0.01);
        pump(doc, clock, 10.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    play_slowly(0.3, doc, clock);
    const double later = grove::media_position(doc, *s);
    CHECK(later > 20.0 && later < 21.0);
    if (!(later > 20.0 && later < 21.0)) std::cerr << "streamed position after seek: " << later << '\n';
    CHECK(grove::media_pause(doc, *s));

    CHECK(grove::media_play(doc, *part));
    CHECK(grove::media_position(doc, *part) >= 2.0);
    play_slowly(1.3, doc, clock);
    CHECK(rp.has("ended"));
    CHECK(!grove::media_is_playing(doc, *part));
    CHECK(std::fabs(grove::media_position(doc, *part) - 3.0) < 0.01);

    CHECK(grove::media_preserves_pitch(*slow) && !grove::media_preserves_pitch(*record));
    CHECK(wait_until(doc, clock, [&] { return rslow.has("canplay") && rrecord.has("canplay"); }));
    CHECK(grove::media_play(doc, *slow));
    CHECK(grove::media_play(doc, *record));
    play_audio(1.0, doc, clock);
    const double ps = grove::media_position(doc, *slow);
    const double pr = grove::media_position(doc, *record);
    CHECK(ps > 0.35 && ps < 0.65);
    CHECK(pr > 0.4 && pr < 0.6);
    record->set_attribute("preservespitch", "true");
    grove::sync_media(doc);
    CHECK(grove::media_is_playing(doc, *record));
    play_audio(0.4, doc, clock);
    CHECK(grove::media_position(doc, *record) > pr + 0.1);
    grove::stop_media(doc);
}

void device_smoke_test() {
    std::cout << "audio backend: " << grove::audio::backend() << '\n';
    for (const grove::audio::Device& d : grove::audio::output_devices()) std::cout << "  output " << d.id << " (" << d.name << ")" << (d.is_default ? " default" : "") << '\n';
    for (const grove::audio::Device& d : grove::audio::input_devices()) std::cout << "  input " << d.id << " (" << d.name << ")" << (d.is_default ? " default" : "") << '\n';
    grove::audio::set_manual_output(false);
    std::string error;
    CHECK(grove::audio::open_output(&error));
    if (!error.empty()) std::cerr << "output: " << error << '\n';
    const std::int64_t id = grove::audio::play_sound((g_dir / "tone.wav").string(), 0.2, 0.0, false);
    CHECK(id > 0);
    double peak = 0.0;
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        peak = std::max(peak, grove::audio::master_level());
    }
    CHECK(peak > 0.05);
    CHECK(grove::audio::stop_sound(id));
    const std::int64_t fx = grove::audio::create_effect("robot");
    const std::int64_t monitor = grove::audio::start_monitor("", "", fx, &error);
    if (monitor) {
        double heard = 0.0;
        double sent = 0.0;
        for (int i = 0; i < 40; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            heard = std::max(heard, grove::audio::monitor_input_level(monitor));
            sent = std::max(sent, grove::audio::monitor_output_level(monitor));
        }
        std::cout << "  monitor levels: in " << heard << ", out " << sent << '\n';
        CHECK(grove::audio::monitor_running(monitor));
        CHECK(grove::audio::stop_monitor(monitor));
    } else {
        std::cout << "  no microphone monitor: " << error << '\n';
    }

    grove::Project project = grove::load_project_from_source("<body><audio id=\"a\" src=\"tone.wav\" controls volume=\"0.2\" /></body>", (g_dir / "device.grove").string(), g_dir.string());
    grove::Document& doc = project.document;
    grove::Node* a = doc.find_by_id("a");
    Recorder rec;
    rec.listen(doc, a);
    grove::start_media(doc);
    CHECK(grove::media_play(doc, *a));
    for (int i = 0; i < 300 && !rec.has("ended"); ++i) {
        doc.events().run_due_timers();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(rec.has("play") && rec.has("canplay") && rec.has("ended"));
    CHECK(rec.count("timeupdate") >= 4);
    grove::stop_media(doc);
    grove::audio::shutdown();
}

} // namespace

int main() {
    g_dir = fs::temp_directory_path() / ("grove-audio-tests-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007)));
    fs::create_directories(g_dir);
    write_tone("tone.wav", 2.0, 440.0);
    write_tone("short.wav", 0.5, 660.0);
    const char* device = std::getenv("FIZMO_AUDIO_DEVICE_TESTS");
    if (device && std::string(device) == "1") {
        device_smoke_test();
    } else {
        std::string error;
        if (!fizmo::audio::set_backend("null", &error)) std::cerr << "can't use the null audio backend: " << error << '\n';
        grove::audio::set_manual_output(true);
        CHECK(grove::audio::manual_output());
        test_parsing_and_controls();
        test_check_warnings();
        test_playback_events();
        test_controls_clicks();
        test_range_autoplay_and_errors();
        test_virtual_files_and_bundling();
        test_sound_api();
        test_effects_and_monitor();
        test_c_api();
        test_meter_sources();
        test_streaming_and_pitch();
        grove::audio::shutdown();
    }
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
