#include "grove/canvas.hpp"

#include <cmath>
#include <string>

namespace grove {

bool is_canvas(const Node& node) {
    return node.is_element() && (node.tag() == "canvas" || node.tag() == "gpu_canvas");
}

bool is_gpu_canvas(const Node& node) {
    return node.is_element() && (node.tag() == "gpu_canvas" || (node.tag() == "canvas" && node.has_attribute("gpu")));
}

std::shared_ptr<CanvasPixels> canvas_of(Document& document, const Node& canvas) {
    auto& all = document.host().canvases;
    auto it = all.find(&canvas);
    if (it != all.end()) return it->second;
    auto made = std::make_shared<CanvasPixels>();
    all.emplace(&canvas, made);
    return made;
}

bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, std::vector<std::uint8_t> rgba) {
    if (!is_canvas(canvas)) return false;
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

bool set_canvas_draw(Document& document, Node& canvas, CanvasDraw draw) {
    if (!is_canvas(canvas)) return false;
    const std::shared_ptr<CanvasPixels> c = canvas_of(document, canvas);
    c->draw = std::move(draw);
    return redraw_canvas(document, canvas);
}

bool clear_canvas_draw(Document& document, Node& canvas) {
    return set_canvas_draw(document, canvas, CanvasDraw());
}

bool has_canvas_draw(const Document& document, const Node& canvas) {
    const auto& all = document.host().canvases;
    const auto it = all.find(&canvas);
    return it != all.end() && static_cast<bool>(it->second->draw);
}

bool redraw_canvas(Document& document, Node& canvas) {
    if (!is_canvas(canvas)) return false;
    const std::shared_ptr<CanvasPixels> c = canvas_of(document, canvas);
    ++c->version;
    canvas.set_attribute("canvas-frame", std::to_string(c->version));
    document.touch();
    return true;
}

bool set_canvas_animating(Document& document, Node& canvas, bool animating) {
    if (!is_canvas(canvas)) return false;
    canvas_of(document, canvas)->animating = animating;
    return redraw_canvas(document, canvas);
}

bool canvas_animating(const Document& document, const Node& canvas) {
    if (canvas.is_element() && canvas.has_attribute("animate") && canvas.attribute_or("animate", "") != "false") return true;
    const auto& all = document.host().canvases;
    const auto it = all.find(&canvas);
    return it != all.end() && it->second->animating;
}

} // namespace grove
