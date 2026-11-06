#include <grove/dom.hpp>

GROVE_ON_LOAD(video_cpp) {
    grove::Element clip = grove::document.get_by_id("clip");
    clip.on("loadeddata", [clip](grove::Event&) {
        std::vector<std::uint8_t> rgba;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        const bool shot = clip.snapshot(rgba, w, h);
        const bool red = shot && rgba.size() == static_cast<std::size_t>(w) * h * 4 && rgba[(60u * w + 40u) * 4u] > 180 && rgba[(60u * w + 40u) * 4u + 2u] < 80;
        const bool sized = clip.video_width() > 159.0 && clip.video_height() > 95.0;
        grove::document.get_by_id("cpp").set_text(shot && red && sized && w == 160 && h == 96 ? "cpp ok" : "cpp failed");
    });
}
