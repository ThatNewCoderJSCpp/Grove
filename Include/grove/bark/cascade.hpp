#ifndef GROVE_BARK_CASCADE_HPP
#define GROVE_BARK_CASCADE_HPP

#include "stylesheet.hpp"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace grove {
namespace bark {

class ComputedStyle {
public:
    const ValueList* get(const std::string& property) const;
    void set(const std::string& property, ValueList values) { m_values[property] = std::move(values); }
    bool has(const std::string& property) const { return m_values.count(property) != 0; }

    const Viewport& viewport() const noexcept { return m_viewport; }
    void set_viewport(const Viewport& viewport) noexcept { m_viewport = viewport; }

    std::optional<Rgba> color(const std::string& property) const;
    std::optional<double> length(const std::string& property, std::size_t index = 0) const;
    std::optional<std::string> text(const std::string& property) const;
    double font_size() const;

    const std::map<std::string, ValueList>& values() const noexcept { return m_values; }

private:
    std::map<std::string, ValueList> m_values;
    Viewport m_viewport;
};

bool is_inherited_property(const std::string& property) noexcept;
void split_align(const ValueList& align, std::string& horizontal, std::string& vertical);
const Stylesheet& default_stylesheet();

class StyleEngine {
public:
    void add(const Stylesheet& sheet);
    void clear() { m_sheet = Stylesheet(); m_cache.clear(); }

    const Stylesheet& sheet() const noexcept { return m_sheet; }

    const Viewport& viewport() const noexcept { return m_viewport; }
    void set_viewport(const Viewport& viewport);

    ComputedStyle compute(const Node& node, const ComputedStyle* parent, Diagnostics& diagnostics) const;
    const ComputedStyle& style_of(const Node& node, Diagnostics& diagnostics);
    void invalidate() { m_cache.clear(); }

private:
    Stylesheet m_sheet;
    std::unordered_map<const Node*, ComputedStyle> m_cache;
    Viewport m_viewport;
};

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_CASCADE_HPP
