#ifndef GROVE_CANVAS_HPP
#define GROVE_CANVAS_HPP

#include "markup/node.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fizmo {
namespace graphics {
class Renderer;
} // namespace graphics
} // namespace fizmo

namespace grove {

struct CanvasRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool shown = false;
};

struct GpuCanvasFrame {
    fizmo::graphics::Renderer& renderer;
    const Node& canvas;
    double width = 0.0;
    double height = 0.0;
    int viewport_x = 0;
    int viewport_y = 0;
    unsigned viewport_width = 0;
    unsigned viewport_height = 0;
    double dt = 0.0;
    std::uint64_t frame = 0;
    bool gpu = false;
};

using CanvasDraw = std::function<void(GpuCanvasFrame&)>;

struct CanvasPixels {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;
    std::uint64_t version = 0;
    CanvasRect rect;
    unsigned reported_width = 0;
    unsigned reported_height = 0;
    CanvasDraw draw;
    bool animating = false;
    std::uint64_t frames = 0;
};

bool is_canvas(const Node& node);
bool is_gpu_canvas(const Node& node);

std::shared_ptr<CanvasPixels> canvas_of(Document& document, const Node& canvas);
bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, std::vector<std::uint8_t> rgba);
bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, const std::uint8_t* rgba);
CanvasRect canvas_rect(const Document& document, const Node& canvas);
bool canvas_size(const Document& document, const Node& canvas, unsigned& width, unsigned& height);

bool set_canvas_draw(Document& document, Node& canvas, CanvasDraw draw);
bool clear_canvas_draw(Document& document, Node& canvas);
bool has_canvas_draw(const Document& document, const Node& canvas);
bool redraw_canvas(Document& document, Node& canvas);
bool set_canvas_animating(Document& document, Node& canvas, bool animating);
bool canvas_animating(const Document& document, const Node& canvas);

} // namespace grove

#endif // GROVE_CANVAS_HPP
