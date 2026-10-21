#ifndef GROVE_CANVAS_HPP
#define GROVE_CANVAS_HPP

#include "markup/node.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace grove {

struct CanvasRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool shown = false;
};

struct CanvasPixels {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;
    std::uint64_t version = 0;
    CanvasRect rect;
    unsigned reported_width = 0;
    unsigned reported_height = 0;
};

std::shared_ptr<CanvasPixels> canvas_of(Document& document, const Node& canvas);
bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, std::vector<std::uint8_t> rgba);
bool set_canvas_pixels(Document& document, Node& canvas, unsigned width, unsigned height, const std::uint8_t* rgba);
CanvasRect canvas_rect(const Document& document, const Node& canvas);
bool canvas_size(const Document& document, const Node& canvas, unsigned& width, unsigned& height);

} // namespace grove

#endif // GROVE_CANVAS_HPP
