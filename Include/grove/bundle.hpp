#ifndef GROVE_BUNDLE_HPP
#define GROVE_BUNDLE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct BundleEntry {
    std::string kind;
    std::string name;
    std::string data;
};

struct Bundle {
    std::vector<BundleEntry> entries;

    void add(std::string kind, std::string name, std::string data);
    void set_meta(const std::string& key, const std::string& value);
    const BundleEntry* find(std::string_view kind, std::string_view name) const;
    std::string meta(std::string_view key, const std::string& fallback = std::string()) const;
    std::vector<const BundleEntry*> all(std::string_view kind) const;
    std::uint64_t hash() const;
};

std::string serialize_bundle(const Bundle& bundle);
std::optional<Bundle> parse_bundle(std::string_view data);

std::optional<std::uint64_t> embedded_bundle_offset(const std::string& executable);
std::optional<Bundle> read_embedded_bundle(const std::string& executable);
bool write_bundled_executable(const std::string& stub, const std::string& output, const Bundle& bundle, std::string& error);

} // namespace grove

#endif // GROVE_BUNDLE_HPP
