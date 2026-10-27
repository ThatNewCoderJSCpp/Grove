#ifndef GROVE_MATH_INPUT_HPP
#define GROVE_MATH_INPUT_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace grove {

struct MathInputGlyph {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool source = true;
    bool visible = true;
    bool italic = false;
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    float width = 0.0f;
    float pad = 0.0f;
};

struct MathInputLine {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float thickness = 1.0f;
};

struct MathInputCaret {
    std::size_t offset = 0;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    bool weak = false;
};

struct MathInputMetrics {
    std::function<float(const std::string& text, float scale, bool italic)> width;
    float ascent = 12.0f;
    float descent = 4.0f;
};

struct MathInputRegion {
    std::size_t upper_begin = 0;
    std::size_t upper_end = 0;
    std::size_t lower_begin = 0;
    std::size_t lower_end = 0;
    std::size_t after = 0;
    bool fraction = false;
    bool has_upper = false;
    bool has_lower = false;
};

struct MathInputLayout {
    std::vector<MathInputGlyph> glyphs;
    std::vector<MathInputRegion> regions;
    std::vector<MathInputLine> lines;
    std::vector<MathInputCaret> carets;
    float width = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float base_ascent = 0.0f;
    float base_descent = 0.0f;
};

MathInputLayout layout_math_input(const std::string& text, const MathInputMetrics& metrics);
MathInputLayout layout_formula_input(const std::string& text, const MathInputMetrics& metrics);
const MathInputCaret& math_input_caret(const MathInputLayout& layout, std::size_t offset);
std::size_t math_input_hit(const MathInputLayout& layout, float x, float y);
std::size_t math_input_left(const MathInputLayout& layout, std::size_t offset);
std::size_t math_input_right(const MathInputLayout& layout, std::size_t offset, std::size_t size);
std::size_t math_input_vertical(const MathInputLayout& layout, std::size_t offset, int direction);

} // namespace grove

#endif // GROVE_MATH_INPUT_HPP
