#include "grove/canvas.hpp"

#include <cmath>
#include <string>

namespace grove {

std::shared_ptr<CanvasPixels> canvas_of(Document& document, const Node& canvas) {
    auto& all = document.host().canvases;
    auto it = all.find(&canvas);
    if (it != all.end()) return it->second;
    auto made = std::make_shared<CanvasPixels>();
    all.emplace(&canvas, made);
    return made;
}

bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, std::vector<std::uint8_t> rgba) {
    if (!canvas.is_element() || canvas.tag() != "canvas") return false;
    const std::size_t need = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < need) return false;
    rgba.resize(need);
    const std::shared_ptr<CanvasPixels> c = canvas_of(document, canvas);
    c->width = width;
    c->height = height;
    c->rgba = std::move(rgba);
    ++c->version;
    canvas.set_attribute("canvas-frame", std::to_string(c->version));
    document.touch();
    return true;
}

bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, const std::uint8_t* rgba) {
    if (!rgba) return false;
    const std::size_t need = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    return set_canvas_pixels(document, canvas, width, height, std::vector<std::uint8_t>(rgba, rgba + need));
}

CanvasRect canvas_rect(const Document& document, const Node& canvas) {
    const auto& all = document.host().canvases;
    const auto it = all.find(&canvas);
    if (it == all.end()) return CanvasRect{};
    return it->second->rect;
}

bool canvas_size(const Document& document, const Node& canvas, unsigned& width, unsigned& height) {
    const CanvasRect r = canvas_rect(document, canvas);
    if (!r.shown) return false;
    width = static_cast<unsigned>(std::max(0.0, std::round(r.width)));
    height = static_cast<unsigned>(std::max(0.0, std::round(r.height)));
    return true;
}

} // namespace grove
