#include "grove/bundle.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace grove {

namespace {

constexpr char k_head[8] = { 'G', 'R', 'O', 'V', 'E', 'P', 'K', '1' };
constexpr char k_tail[8] = { 'G', 'R', 'O', 'V', 'E', 'E', 'N', 'D' };

void put_u64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

std::uint64_t get_u64(const char* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

void put_string(std::string& out, std::string_view s) {
    put_u64(out, s.size());
    out.append(s.data(), s.size());
}

bool get_string(std::string_view data, std::size_t& pos, std::string& out) {
    if (data.size() - pos < 8) return false;
    const std::uint64_t n = get_u64(data.data() + pos);
    pos += 8;
    if (n > data.size() - pos) return false;
    out.assign(data.data() + pos, static_cast<std::size_t>(n));
    pos += static_cast<std::size_t>(n);
    return true;
}

} // namespace

void Bundle::add(std::string kind, std::string name, std::string data) {
    entries.push_back(BundleEntry{ std::move(kind), std::move(name), std::move(data) });
}

void Bundle::set_meta(const std::string& key, const std::string& value) {
    for (BundleEntry& e : entries) {
        if (e.kind == "meta" && e.name == key) {
            e.data = value;
            return;
        }
    }
    add("meta", key, value);
}

const BundleEntry* Bundle::find(std::string_view kind, std::string_view name) const {
    for (const BundleEntry& e : entries) {
        if (e.kind == kind && e.name == name) return &e;
    }
    return nullptr;
}

std::string Bundle::meta(std::string_view key, const std::string& fallback) const {
    const BundleEntry* e = find("meta", key);
    return e ? e->data : fallback;
}

std::vector<const BundleEntry*> Bundle::all(std::string_view kind) const {
    std::vector<const BundleEntry*> out;
    for (const BundleEntry& e : entries) {
        if (e.kind == kind) out.push_back(&e);
    }
    return out;
}

std::uint64_t Bundle::hash() const {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](std::string_view s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= 0xFF;
        h *= 1099511628211ULL;
    };
    for (const BundleEntry& e : entries) {
        mix(e.kind);
        mix(e.name);
        mix(e.data);
    }
    return h;
}

std::string serialize_bundle(const Bundle& bundle) {
    std::string out(k_head, sizeof k_head);
    put_u64(out, bundle.entries.size());
    for (const BundleEntry& e : bundle.entries) {
        put_string(out, e.kind);
        put_string(out, e.name);
        put_string(out, e.data);
    }
    return out;
}

std::optional<Bundle> parse_bundle(std::string_view data) {
    if (data.size() < sizeof k_head + 8 || std::memcmp(data.data(), k_head, sizeof k_head) != 0) return std::nullopt;
    std::size_t pos = sizeof k_head;
    const std::uint64_t count = get_u64(data.data() + pos);
    pos += 8;
    Bundle bundle;
    for (std::uint64_t i = 0; i < count; ++i) {
        BundleEntry e;
        if (!get_string(data, pos, e.kind) || !get_string(data, pos, e.name) || !get_string(data, pos, e.data)) return std::nullopt;
        bundle.entries.push_back(std::move(e));
    }
    return bundle;
}

std::optional<std::uint64_t> embedded_bundle_offset(const std::string& executable) {
    std::ifstream in(executable, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 16) return std::nullopt;
    in.seekg(size - 16);
    char tail[16];
    if (!in.read(tail, 16)) return std::nullopt;
    if (std::memcmp(tail + 8, k_tail, sizeof k_tail) != 0) return std::nullopt;
    const std::uint64_t offset = get_u64(tail);
    if (offset >= static_cast<std::uint64_t>(size - 16)) return std::nullopt;
    return offset;
}

std::optional<Bundle> read_embedded_bundle(const std::string& executable) {
    const std::optional<std::uint64_t> offset = embedded_bundle_offset(executable);
    if (!offset) return std::nullopt;
    std::ifstream in(executable, std::ios::binary);
    in.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(in.tellg());
    const std::uint64_t length = size - 16 - *offset;
    std::string data(static_cast<std::size_t>(length), '\0');
    in.seekg(static_cast<std::streamoff>(*offset));
    if (!in.read(data.data(), static_cast<std::streamsize>(length))) return std::nullopt;
    return parse_bundle(data);
}

bool write_bundled_executable(const std::string& stub, const std::string& output, const Bundle& bundle, std::string& error) {
    std::ifstream in(stub, std::ios::binary);
    if (!in) {
        error = "can't read '" + stub + "'";
        return false;
    }
    in.seekg(0, std::ios::end);
    std::uint64_t stub_size = static_cast<std::uint64_t>(in.tellg());
    if (const std::optional<std::uint64_t> existing = embedded_bundle_offset(stub)) stub_size = *existing;
    std::string bytes(static_cast<std::size_t>(stub_size), '\0');
    in.seekg(0);
    if (!in.read(bytes.data(), static_cast<std::streamsize>(stub_size))) {
        error = "can't read '" + stub + "'";
        return false;
    }
    in.close();

    std::error_code ec;
    const fs::path out_path(output);
    if (out_path.has_parent_path()) fs::create_directories(out_path.parent_path(), ec);
    const fs::path temp = out_path.string() + ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "can't write '" + temp.string() + "'";
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const std::string payload = serialize_bundle(bundle);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        std::string tail;
        put_u64(tail, stub_size);
        tail.append(k_tail, sizeof k_tail);
        out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
        if (!out) {
            error = "couldn't finish writing '" + temp.string() + "'";
            return false;
        }
    }
    fs::permissions(temp, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, ec);
    fs::remove(out_path, ec);
    fs::rename(temp, out_path, ec);
    if (ec) {
        error = "can't replace '" + output + "': " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace grove
