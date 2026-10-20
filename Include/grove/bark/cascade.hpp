#ifndef GROVE_BARK_CASCADE_HPP
#define GROVE_BARK_CASCADE_HPP

#include "stylesheet.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace grove {
namespace bark {

class ComputedStyle {
public:
    ComputedStyle() = default;
    ComputedStyle(const ComputedStyle& other) : m_values(other.m_values), m_viewport(other.m_viewport) {}
    ComputedStyle(ComputedStyle&& other) noexcept : m_values(std::move(other.m_values)), m_viewport(other.m_viewport) {}
    ComputedStyle& operator=(const ComputedStyle& other) {
        if (this != &other) {
            m_values = other.m_values;
            m_viewport = other.m_viewport;
            m_index.clear();
        }
        return *this;
    }
    ComputedStyle& operator=(ComputedStyle&& other) noexcept {
        if (this != &other) {
            m_values = std::move(other.m_values);
            m_viewport = other.m_viewport;
            m_index.clear();
            other.m_index.clear();
        }
        return *this;
    }

    const ValueList* get(const std::string& property) const { return get_view(property); }
    const ValueList* get(const char* property) const { return get_view(property); }
    const ValueList* get_view(std::string_view property) const;
    void set(const std::string& property, ValueList values) {
        m_values[property] = std::move(values);
        m_index.clear();
    }
    bool has(const std::string& property) const { return get(property) != nullptr; }

    const Viewport& viewport() const noexcept { return m_viewport; }
    void set_viewport(const Viewport& viewport) noexcept { m_viewport = viewport; }

    std::optional<Rgba> color(const std::string& property) const;
    std::optional<double> length(const std::string& property, std::size_t index = 0) const;
    std::optional<std::string> text(const std::string& property) const;
    std::string_view text_view(std::string_view property) const;
    std::string_view text_view(const char* property) const { return text_view(std::string_view(property)); }
    std::string_view text_view(const std::string& property) const { return text_view(std::string_view(property)); }
    double font_size() const;

    const std::map<std::string, ValueList>& values() const noexcept { return m_values; }

private:
    struct Slot {
        std::uint64_t hash = 0;
        const std::string* name = nullptr;
        const ValueList* values = nullptr;
    };

    void build_index() const;

    std::map<std::string, ValueList> m_values;
    Viewport m_viewport;
    mutable std::vector<Slot> m_index;
};

bool is_inherited_property(const std::string& property) noexcept;
void split_align(const ValueList& align, std::string& horizontal, std::string& vertical);
const Stylesheet& default_stylesheet();

class StyleEngine {
public:
    void add(const Stylesheet& sheet);
    void clear() {
        m_sheet = Stylesheet();
        m_cache.clear();
        m_recent = RecentSlot();
        m_index_ready = false;
        ++m_revision;
    }

    const Stylesheet& sheet() const noexcept { return m_sheet; }

    const Viewport& viewport() const noexcept { return m_viewport; }
    void set_viewport(const Viewport& viewport);

    ComputedStyle compute(const Node& node, const ComputedStyle* parent, Diagnostics& diagnostics) const;
    const ComputedStyle& style_of(const Node& node, Diagnostics& diagnostics);
    void invalidate() {
        ++m_epoch;
        m_recent = RecentSlot();
        if (m_cache.size() > m_prune_at) prune();
    }
    std::size_t cached_count() const noexcept { return m_cache.size(); }
    std::uint64_t revision() const noexcept { return m_revision; }
    std::uint64_t style_key(const Node& node, Diagnostics& diagnostics);

private:
    struct Entry {
        std::uint64_t key = 0;
        std::uint64_t checked = 0;
        ComputedStyle style;
    };

    struct RuleIndex {
        std::unordered_map<std::string, std::vector<std::size_t>> by_id;
        std::unordered_map<std::string, std::vector<std::size_t>> by_class;
        std::unordered_map<std::string, std::vector<std::size_t>> by_tag;
        std::vector<std::size_t> universal;
        std::vector<std::size_t> first_order;
        bool uses_position = false;
        bool uses_empty = false;
        bool uses_current = false;
    };

    void build_index() const;
    void prune();
    std::uint64_t key_of(const Node& node, std::uint64_t parent_key) const;

    Stylesheet m_sheet;
    std::unordered_map<const Node*, Entry> m_cache;
    Viewport m_viewport;
    std::uint64_t m_epoch = 1;
    std::size_t m_prune_at = 4096;
    std::uint64_t m_revision = 1;
    struct RecentSlot {
        const Node* node = nullptr;
        Entry* entry = nullptr;
        RecentSlot() = default;
        RecentSlot(const RecentSlot&) {}
        RecentSlot& operator=(const RecentSlot&) {
            node = nullptr;
            entry = nullptr;
            return *this;
        }
    };
    RecentSlot m_recent;
    mutable RuleIndex m_index;
    mutable bool m_index_ready = false;
};

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_CASCADE_HPP
