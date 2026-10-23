#include "grove/android_package.hpp"
#include "grove/bundle.hpp"
#include "grove/fizmo_android_dex.hpp"
#include "grove/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace grove {

namespace {

const char* const kSdkHint = "sdkmanager \"ndk;27.2.12479018\" \"build-tools;35.0.0\" \"platforms;android-35\"";

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

fs::path home_dir() {
#if defined(_WIN32)
    const std::string profile = env("USERPROFILE");
    if (!profile.empty()) return fs::path(profile);
#endif
    return fs::path(env("HOME"));
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string collapse_whitespace(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

int to_int(const std::string& s, int fallback) {
    if (!all_digits(s) || s.size() > 9) return fallback;
    return std::stoi(s);
}

bool valid_package_name(const std::string& name) {
    std::size_t parts = 0;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t dot = std::min(name.find('.', start), name.size());
        const std::string part = name.substr(start, dot - start);
        if (part.empty() || !std::isalpha(static_cast<unsigned char>(part[0]))) return false;
        for (char c : part) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
        }
        ++parts;
        start = dot + 1;
    }
    return parts >= 2;
}

bool valid_abi(const std::string& abi) {
    return abi == "arm64-v8a" || abi == "armeabi-v7a" || abi == "x86_64" || abi == "x86";
}

std::string abi_triple(const std::string& abi) {
    if (abi == "arm64-v8a") return "aarch64-linux-android";
    if (abi == "armeabi-v7a") return "armv7a-linux-androideabi";
    if (abi == "x86_64") return "x86_64-linux-android";
    return "i686-linux-android";
}

std::string host_tag() {
#if defined(_WIN32)
    return "windows-x86_64";
#elif defined(__APPLE__)
    return "darwin-x86_64";
#else
    return "linux-x86_64";
#endif
}

std::string exe_name(const std::string& base) {
#if defined(_WIN32)
    return base + ".exe";
#else
    return base;
#endif
}

std::string script_name(const std::string& base) {
#if defined(_WIN32)
    return base + ".bat";
#else
    return base;
#endif
}

std::string wrapper_name(const std::string& base) {
#if defined(_WIN32)
    return base + ".cmd";
#else
    return base;
#endif
}

std::string find_on_path(const std::string& name) {
    const std::string path = env("PATH");
#if defined(_WIN32)
    const char sep = ';';
    const std::vector<std::string> suffixes = { "", ".exe", ".bat", ".cmd" };
#else
    const char sep = ':';
    const std::vector<std::string> suffixes = { "" };
#endif
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(sep, start), path.size());
        const std::string dir = path.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) continue;
        for (const std::string& suffix : suffixes) {
            const fs::path p = fs::path(dir) / (name + suffix);
            if (file_exists(p)) return p.string();
        }
    }
    return {};
}

std::vector<long> version_parts(const std::string& name) {
    std::vector<long> parts;
    std::string digits;
    for (char c : name + ".") {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        } else if (!digits.empty()) {
            parts.push_back(digits.size() > 9 ? 999999999L : std::stol(digits));
            digits.clear();
        }
    }
    return parts;
}

std::vector<fs::path> subdirs_newest_first(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!dir_exists(dir)) return out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        const std::vector<long> va = version_parts(a.filename().string());
        const std::vector<long> vb = version_parts(b.filename().string());
        if (va != vb) return va > vb;
        return a.filename().string() > b.filename().string();
    });
    return out;
}

std::string quote_arg(const std::string& a) {
#if defined(_WIN32)
    std::string out = "\"";
    for (char c : a) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    return out + "\"";
#else
    std::string out = "'";
    for (char c : a) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

std::string shown_command(const std::vector<std::string>& args) {
    std::string out;
    bool secret = false;
    for (const std::string& a : args) {
        if (!out.empty()) out += ' ';
        if (secret && a.rfind("env:", 0) != 0 && a.rfind("file:", 0) != 0) out += "'***'";
        else out += quote_arg(a);
        secret = a == "--ks-pass" || a == "--key-pass" || a == "-storepass" || a == "-keypass";
    }
    return out;
}

std::optional<std::string> read_binary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_binary(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::string last_lines(const std::string& text, std::size_t count) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t.rfind("at ", 0) == 0 || t.rfind("... ", 0) == 0 || t.find("Picked up JAVA_TOOL_OPTIONS") != std::string::npos) continue;
        lines.push_back(line);
    }
    std::string out;
    const std::size_t from = lines.size() > count ? lines.size() - count : 0;
    for (std::size_t i = from; i < lines.size(); ++i) out += "  " + lines[i] + "\n";
    if (!out.empty()) out.pop_back();
    return out;
}

struct Runner {
    fs::path work;
    bool verbose = false;
    std::ostream* log = nullptr;

    int run(const std::vector<std::string>& args, std::string& output) const {
        std::string command;
        for (const std::string& a : args) {
            if (!command.empty()) command += ' ';
            command += quote_arg(a);
        }
        if (verbose && log) *log << shown_command(args) << '\n';
        const fs::path out_path = work / "tool.log";
        command += " > " + quote_arg(out_path.string()) + " 2>&1";
#if defined(_WIN32)
        command = "\"" + command + "\"";
#endif
        const int status = std::system(command.c_str());
        output = read_binary(out_path).value_or(std::string());
        std::error_code ec;
        fs::remove(out_path, ec);
#if defined(_WIN32)
        return status;
#else
        if (status == -1) return -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    }
};

struct Kit {
    std::string abi;
    fs::path dir;
    int api = 26;
    std::string grove;
    std::vector<std::string> runtime_libs;
    std::vector<std::string> optional_archives;
    std::vector<std::string> optional_libs;
};

std::vector<std::string> kit_list(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(';', start), text.size());
        const std::string item = trim(text.substr(start, end - start));
        if (!item.empty()) out.push_back(item);
        start = end + 1;
    }
    return out;
}

Kit parse_kit(const fs::path& dir, const std::string& abi, const std::string& text) {
    Kit kit;
    kit.abi = abi;
    kit.dir = dir;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "abi") kit.abi = value;
        else if (key == "api") kit.api = to_int(value, 26);
        else if (key == "grove") kit.grove = value;
        else if (key == "runtime-libs") kit.runtime_libs = kit_list(value);
        else if (key == "optional-archives") kit.optional_archives = kit_list(value);
        else if (key == "optional-libs") kit.optional_libs = kit_list(value);
    }
    return kit;
}

std::vector<std::pair<std::string, fs::path>> kit_roots(const AndroidBuildOptions& options) {
    std::vector<std::pair<std::string, fs::path>> roots;
    if (!options.kit_dir.empty()) roots.emplace_back("--android-kit", fs::absolute(options.kit_dir));
    if (!env("GROVE_ANDROID_KIT").empty()) roots.emplace_back("GROVE_ANDROID_KIT", fs::absolute(env("GROVE_ANDROID_KIT")));
#ifdef GROVE_ANDROID_KIT_DIR
    if (std::string(GROVE_ANDROID_KIT_DIR).size() > 0) roots.emplace_back("built in", fs::path(GROVE_ANDROID_KIT_DIR));
#endif
    const fs::path exe = executable_path();
    if (!exe.empty()) roots.emplace_back("next to grove", (exe.parent_path().parent_path() / "lib" / "grove" / "android").lexically_normal());
    return roots;
}

std::string join_kit_items(const Kit& kit, const std::vector<std::string>& items, std::vector<std::string>& missing) {
    std::string out;
    for (const std::string& item : items) {
        std::string value = item;
        if (item[0] != '-') {
            const fs::path p = fs::path(item).is_absolute() ? fs::path(item) : kit.dir / item;
            if (!file_exists(p)) missing.push_back(p.string());
            value = p.string();
        }
        if (!out.empty()) out += ';';
        out += value;
    }
    return out;
}

std::vector<fs::path> sdk_roots(const AndroidBuildOptions& options) {
    std::vector<fs::path> roots;
    auto add = [&roots](const fs::path& p) {
        if (p.empty() || !dir_exists(p)) return;
        const fs::path n = fs::absolute(p).lexically_normal();
        for (const fs::path& r : roots) {
            if (r == n) return;
        }
        roots.push_back(n);
    };
    add(options.sdk);
    add(env("ANDROID_HOME"));
    add(env("ANDROID_SDK_ROOT"));
    const fs::path home = home_dir();
    if (!home.empty()) {
        add(home / "Android" / "Sdk");
        add(home / "Library" / "Android" / "sdk");
    }
    if (!env("LOCALAPPDATA").empty()) add(fs::path(env("LOCALAPPDATA")) / "Android" / "Sdk");
#if !defined(_WIN32)
    add("/usr/lib/android-sdk");
#endif
    return roots;
}

std::string find_build_tool(const std::vector<fs::path>& sdks, const std::string& file, const std::string& bare) {
    for (const fs::path& sdk : sdks) {
        for (const fs::path& bt : subdirs_newest_first(sdk / "build-tools")) {
            if (file_exists(bt / file)) return (bt / file).string();
        }
    }
    return find_on_path(bare);
}

struct Platform {
    fs::path jar;
    int api = 0;
};

Platform find_platform(const std::vector<fs::path>& sdks) {
    Platform best;
    for (const fs::path& sdk : sdks) {
        for (const fs::path& p : subdirs_newest_first(sdk / "platforms")) {
            if (!file_exists(p / "android.jar")) continue;
            const std::vector<long> v = version_parts(p.filename().string());
            const int api = v.empty() ? 0 : static_cast<int>(v[0]);
            if (best.jar.empty() || api > best.api) {
                best.jar = p / "android.jar";
                best.api = api;
            }
            break;
        }
    }
    return best;
}

bool valid_ndk(const fs::path& ndk) {
    return dir_exists(ndk / "toolchains" / "llvm" / "prebuilt" / host_tag() / "bin");
}

fs::path find_ndk(const AndroidBuildOptions& options, const std::vector<fs::path>& sdks, std::string& problem) {
    if (!options.ndk.empty()) {
        if (valid_ndk(options.ndk)) return fs::absolute(options.ndk);
        problem = "--ndk '" + options.ndk + "' isn't an Android NDK (no toolchains/llvm/prebuilt/" + host_tag() + "/bin in it)";
        return {};
    }
    for (const char* name : { "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK" }) {
        const std::string v = env(name);
        if (v.empty()) continue;
        if (valid_ndk(v)) return fs::path(v);
        problem = std::string(name) + " is '" + v + "', but that isn't an Android NDK for " + host_tag();
    }
    for (const fs::path& sdk : sdks) {
        for (const fs::path& n : subdirs_newest_first(sdk / "ndk")) {
            if (valid_ndk(n)) return n;
        }
        if (valid_ndk(sdk / "ndk-bundle")) return sdk / "ndk-bundle";
    }
    return {};
}

std::string xml_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            case '\n': out += "&#10;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string aapt_value(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '@' || c == '?') {
            if (out.empty()) out += '\\';
        }
        out += c;
    }
    return out;
}

std::uint16_t rd16(const std::string& s, std::size_t o) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(s[o]) | (static_cast<unsigned char>(s[o + 1]) << 8));
}

std::uint32_t rd32(const std::string& s, std::size_t o) {
    return static_cast<std::uint32_t>(rd16(s, o)) | (static_cast<std::uint32_t>(rd16(s, o + 2)) << 16);
}

void wr16(std::string& s, std::uint32_t v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>((v >> 8) & 0xFF);
}

void wr32(std::string& s, std::uint32_t v) {
    wr16(s, v & 0xFFFF);
    wr16(s, v >> 16);
}

bool ascii_only(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

std::string orientation_value(const std::string& o) {
    if (o == "portrait") return "portrait";
    if (o == "landscape") return "landscape";
    if (o == "sensor") return "fullSensor";
    if (o == "any") return "unspecified";
    if (o == "locked") return "locked";
    return {};
}

std::string keystore_pass(const std::string& v) {
    if (v.rfind("pass:", 0) == 0 || v.rfind("env:", 0) == 0 || v.rfind("file:", 0) == 0 || v == "stdin") return v;
    return "pass:" + v;
}

fs::path debug_keystore_path() {
    const std::string user = env("ANDROID_USER_HOME");
    if (!user.empty()) return fs::path(user) / "debug.keystore";
    return home_dir() / ".android" / "debug.keystore";
}

std::string find_keytool() {
    const std::string java_home = env("JAVA_HOME");
    if (!java_home.empty() && file_exists(fs::path(java_home) / "bin" / exe_name("keytool"))) return (fs::path(java_home) / "bin" / exe_name("keytool")).string();
    return find_on_path("keytool");
}

std::string find_adb(const std::vector<fs::path>& sdks) {
    for (const fs::path& sdk : sdks) {
        if (file_exists(sdk / "platform-tools" / exe_name("adb"))) return (sdk / "platform-tools" / exe_name("adb")).string();
    }
    return find_on_path("adb");
}

} // namespace

std::uint32_t zip_crc32(const std::string& data) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool append_zip_entries(const std::string& input, const std::string& output, const std::vector<ZipAddition>& additions, std::string& error) {
    const std::optional<std::string> read = read_binary(input);
    if (!read) {
        error = "can't read '" + input + "'";
        return false;
    }
    const std::string& zip = *read;
    if (zip.size() < 22) {
        error = "'" + input + "' isn't a zip file";
        return false;
    }
    std::size_t eocd = std::string::npos;
    const std::size_t lowest = zip.size() > 22 + 65535 ? zip.size() - 22 - 65535 : 0;
    for (std::size_t i = zip.size() - 22 + 1; i-- > lowest;) {
        if (rd32(zip, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "'" + input + "' isn't a zip file";
        return false;
    }
    const std::uint16_t count = rd16(zip, eocd + 10);
    const std::uint32_t cd_size = rd32(zip, eocd + 12);
    const std::uint32_t cd_offset = rd32(zip, eocd + 16);
    const std::uint16_t comment_len = rd16(zip, eocd + 20);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
        error = "'" + input + "' is a zip64 file, which grove can't add to";
        return false;
    }
    if (static_cast<std::uint64_t>(cd_offset) + cd_size > eocd) {
        error = "'" + input + "' has a damaged zip directory";
        return false;
    }

    std::vector<std::string> records;
    std::size_t p = cd_offset;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (p + 46 > eocd || rd32(zip, p) != 0x02014b50u) {
            error = "'" + input + "' has a damaged zip directory";
            return false;
        }
        const std::size_t len = 46 + rd16(zip, p + 28) + rd16(zip, p + 30) + rd16(zip, p + 32);
        if (p + len > eocd) {
            error = "'" + input + "' has a damaged zip directory";
            return false;
        }
        const std::string name = zip.substr(p + 46, rd16(zip, p + 28));
        bool replaced = false;
        for (const ZipAddition& a : additions) replaced = replaced || a.name == name;
        if (!replaced) records.push_back(zip.substr(p, len));
        p += len;
    }

    std::string out = zip.substr(0, cd_offset);
    for (const ZipAddition& a : additions) {
        if (a.data.size() >= 0xFFFFFFFFu || out.size() >= 0xFFFFFFFFu || a.name.size() > 0xFFFF) {
            error = "'" + a.name + "' is too big for a zip file";
            return false;
        }
        const std::uint32_t crc = zip_crc32(a.data);
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t flags = ascii_only(a.name) ? 0 : 0x0800;
        const std::uint32_t size = static_cast<std::uint32_t>(a.data.size());
        const std::uint32_t name_len = static_cast<std::uint32_t>(a.name.size());
        wr32(out, 0x04034b50u);
        wr16(out, 10);
        wr16(out, flags);
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0x21);
        wr32(out, crc);
        wr32(out, size);
        wr32(out, size);
        wr16(out, name_len);
        wr16(out, 0);
        out += a.name;
        out += a.data;

        std::string r;
        wr32(r, 0x02014b50u);
        wr16(r, 20);
        wr16(r, 10);
        wr16(r, flags);
        wr16(r, 0);
        wr16(r, 0);
        wr16(r, 0x21);
        wr32(r, crc);
        wr32(r, size);
        wr32(r, size);
        wr16(r, name_len);
        wr16(r, 0);
        wr16(r, 0);
        wr16(r, 0);
        wr16(r, 0);
        wr32(r, 0);
        wr32(r, offset);
        r += a.name;
        records.push_back(std::move(r));
    }
    if (records.size() > 0xFFFF || out.size() >= 0xFFFFFFFFu) {
        error = "the APK has too many files for a zip file";
        return false;
    }
    const std::uint32_t new_cd_offset = static_cast<std::uint32_t>(out.size());
    for (const std::string& r : records) out += r;
    const std::uint64_t new_cd_size = out.size() - new_cd_offset;
    if (out.size() >= 0xFFFFFFFFu) {
        error = "the APK is too big for a zip file";
        return false;
    }
    wr32(out, 0x06054b50u);
    wr16(out, 0);
    wr16(out, 0);
    wr16(out, static_cast<std::uint32_t>(records.size()));
    wr16(out, static_cast<std::uint32_t>(records.size()));
    wr32(out, static_cast<std::uint32_t>(new_cd_size));
    wr32(out, new_cd_offset);
    wr16(out, comment_len);
    out += zip.substr(eocd + 22, comment_len);

    if (!write_binary(output, out)) {
        error = "can't write '" + output + "'";
        return false;
    }
    return true;
}

std::string default_android_output(const std::string& document_path) {
    fs::path p(document_path);
    p.replace_extension(".apk");
    return p.string();
}

std::string default_android_package(const std::string& document_path) {
    std::string stem = fs::path(document_path).stem().string();
    std::string part;
    for (char c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) && u < 0x80) part += static_cast<char>(std::tolower(u));
        else if (!part.empty() && part.back() != '_') part += '_';
    }
    while (!part.empty() && part.back() == '_') part.pop_back();
    if (part.empty()) part = "app";
    if (!std::isalpha(static_cast<unsigned char>(part[0]))) part = "app_" + part;
    return "org.grove." + part;
}

std::string android_app_label(const Project& project) {
    const std::string label = collapse_whitespace(project.document.declaration("label"));
    if (!label.empty()) return label;
    if (const Node* t = project.document.first_by_tag("title")) {
        const std::string title = collapse_whitespace(t->inner_text());
        if (!title.empty()) return title;
    }
    return fs::path(project.path).stem().string();
}

std::string android_manifest_xml(const AndroidManifestInfo& info) {
    std::string changes = "orientation|screenSize|screenLayout|smallestScreenSize|keyboard|keyboardHidden|navigation|uiMode|";
    if (info.platform_api == 0 || info.platform_api >= 24) changes += "density|";
    changes += "fontScale|locale|layoutDirection";
    const std::string label = xml_escape(aapt_value(info.label));
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    x += "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n";
    x += "    package=\"" + xml_escape(info.package) + "\"\n";
    x += "    android:versionCode=\"" + xml_escape(info.version_code) + "\"\n";
    x += "    android:versionName=\"" + xml_escape(aapt_value(info.version_name)) + "\">\n";
    x += "    <uses-sdk android:minSdkVersion=\"" + xml_escape(info.min_sdk) + "\" android:targetSdkVersion=\"" + xml_escape(info.target_sdk) + "\"/>\n";
    x += "    <uses-permission android:name=\"android.permission.INTERNET\"/>\n";
    x += "    <uses-permission android:name=\"android.permission.VIBRATE\"/>\n";
    x += "    <application android:label=\"" + label + "\" android:hasCode=\"" + std::string(info.has_code ? "true" : "false") + "\" android:extractNativeLibs=\"true\"";
    if (info.icon) x += " android:icon=\"@mipmap/ic_launcher\"";
    x += ">\n";
    x += "        <activity android:name=\"" + xml_escape(info.activity) + "\"\n";
    x += "            android:label=\"" + label + "\"\n";
    x += "            android:exported=\"true\"\n";
    x += "            android:configChanges=\"" + changes + "\"\n";
    x += "            android:windowSoftInputMode=\"adjustResize\"\n";
    const std::string orientation = orientation_value(info.orientation);
    if (!orientation.empty()) x += "            android:screenOrientation=\"" + orientation + "\"\n";
    x += "            android:theme=\"@android:style/Theme.DeviceDefault.NoActionBar\">\n";
    x += "            <meta-data android:name=\"android.app.lib_name\" android:value=\"grove_android\"/>\n";
    x += "            <intent-filter>\n";
    x += "                <action android:name=\"android.intent.action.MAIN\"/>\n";
    x += "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n";
    x += "            </intent-filter>\n";
    x += "        </activity>\n";
    x += "    </application>\n";
    x += "</manifest>\n";
    return x;
}

namespace {

bool package_apk(Project& project, const std::string& document_path, const AndroidBuildOptions& options, AndroidBuildResult& result, const std::vector<std::pair<std::string, std::string>>& reads, std::ostream* log) {
    Diagnostics& diag = project.diagnostics;
    const Document& doc = project.document;
    const Span span = doc.declaration_span();
    const std::string& file = project.path;
    auto fail = [&](const std::string& message) {
        diag.error(file, span, message);
        return false;
    };

    const fs::path doc_abs = fs::absolute(document_path).lexically_normal();
    const fs::path base = doc_abs.parent_path();
    const std::string stem = doc_abs.stem().string();

    if (!project.android()) {
        const std::string package = doc.declaration("package");
        if (!package.empty() && !valid_package_name(package)) return fail("package \"" + package + "\" isn't an Android package name like com.example.app");
        for (const char* key : { "version-code", "min-sdk", "target-sdk" }) {
            const std::string v = doc.declaration(key);
            if (!v.empty() && !all_digits(v)) return fail(std::string("'") + key + "' has to be a whole number");
        }
    }

    std::vector<std::string> abis;
    std::vector<std::string> requested = options.abis;
    if (requested.empty()) requested = split_asset_list(doc.declaration("abi"));
    if (requested.empty()) requested.push_back("arm64-v8a");
    for (const std::string& abi : requested) {
        if (!valid_abi(abi)) return fail("abi \"" + abi + "\" isn't arm64-v8a, armeabi-v7a, x86_64 or x86");
        if (std::find(abis.begin(), abis.end(), abi) == abis.end()) abis.push_back(abi);
    }

    const std::vector<std::pair<std::string, fs::path>> roots = kit_roots(options);
    std::vector<Kit> kits;
    for (const std::string& abi : abis) {
        bool found = false;
        for (const auto& [label, root] : roots) {
            const fs::path dir = root / abi;
            const std::optional<std::string> text = read_binary(dir / "kit.txt");
            if (!text || !file_exists(dir / "libgrove_android.so")) continue;
            Kit kit = parse_kit(dir, abi, *text);
            if (kit.abi != abi) return fail("the Android kit in '" + dir.string() + "' is for " + kit.abi + ", not " + abi);
            kits.push_back(std::move(kit));
            found = true;
            break;
        }
        if (!found) {
            std::string message = "there's no Grove Android kit for " + abi + " (a folder with kit.txt and libgrove_android.so); looked in:";
            for (const auto& [label, root] : roots) message += "\n  " + (root / abi).string() + " (" + label + ")";
            message += "\nbuild it with Grove's Android CMake (the grove_android_kit target) and pass the kit folder with --android-kit <dir> or GROVE_ANDROID_KIT";
            return fail(message);
        }
    }
    for (const Kit& kit : kits) {
        if (!kit.grove.empty() && kit.grove != GROVE_VERSION_STRING) {
            diag.warning(file, span, "the Android kit for " + kit.abi + " is from Grove " + kit.grove + ", but this is Grove " + std::string(GROVE_VERSION_STRING) + "; rebuild the kit if the app misbehaves");
        }
    }

    int min_sdk = to_int(doc.declaration("min-sdk"), 26);
    for (const Kit& kit : kits) {
        if (kit.api > min_sdk) {
            if (doc.has_declaration("min-sdk")) diag.warning(file, span, "the Android kit for " + kit.abi + " needs API " + std::to_string(kit.api) + ", so min-sdk is raised to " + std::to_string(kit.api));
            min_sdk = kit.api;
        }
    }
    const int target_sdk = to_int(doc.declaration("target-sdk"), std::max(35, min_sdk));
    if (target_sdk < min_sdk) return fail("target-sdk " + std::to_string(target_sdk) + " is lower than min-sdk " + std::to_string(min_sdk));

    std::size_t runnable = 0;
    for (const ScriptBlock& s : project.scripts) runnable += is_header(s.language) ? 0 : 1;

    const std::vector<fs::path> sdks = sdk_roots(options);
    if (!options.sdk.empty() && !dir_exists(options.sdk)) return fail("--sdk '" + options.sdk + "' isn't a folder");

    std::string ndk_problem;
    fs::path ndk_bin;
    if (runnable > 0) {
        const fs::path ndk = find_ndk(options, sdks, ndk_problem);
        if (ndk.empty()) {
            std::string message = ndk_problem.empty() ? std::string("this app has scripts, so it needs the Android NDK to compile them, and grove can't find one") : ndk_problem;
            message += "\ninstall it with " + std::string(kSdkHint) + " (sdkmanager is in the Android SDK's cmdline-tools), then pass --ndk <dir> or set ANDROID_NDK_HOME";
            return fail(message);
        }
        ndk_bin = ndk / "toolchains" / "llvm" / "prebuilt" / host_tag() / "bin";
        if (options.tools.verbose && log) *log << "grove: using the NDK in " << ndk.string() << '\n';
    }

    const std::string aapt2 = find_build_tool(sdks, exe_name("aapt2"), "aapt2");
    const std::string aapt = aapt2.empty() ? find_build_tool(sdks, exe_name("aapt"), "aapt") : std::string();
    const std::string zipalign = find_build_tool(sdks, exe_name("zipalign"), "zipalign");
    const std::string apksigner = find_build_tool(sdks, script_name("apksigner"), "apksigner");
    const Platform platform = find_platform(sdks);
    const bool debug_key = options.keystore.empty();
    const fs::path keystore = debug_key ? debug_keystore_path() : fs::absolute(options.keystore);
    const std::string keytool = debug_key && !file_exists(keystore) ? find_keytool() : std::string();

    std::vector<std::string> missing;
    if (aapt2.empty() && aapt.empty()) missing.push_back("aapt2 (Android SDK build-tools)");
    if (zipalign.empty()) missing.push_back("zipalign (Android SDK build-tools)");
    if (apksigner.empty()) missing.push_back("apksigner (Android SDK build-tools)");
    if (platform.jar.empty()) missing.push_back("android.jar (an Android SDK platform)");
    if (!missing.empty()) {
        std::string message = "grove can't make an APK without:";
        for (const std::string& m : missing) message += "\n  " + m;
        if (sdks.empty()) {
            message += "\nno Android SDK was found; pass --sdk <dir> or set ANDROID_HOME";
        } else {
            message += "\nlooked in:";
            for (const fs::path& s : sdks) message += "\n  " + s.string();
            message += "\nand on PATH";
        }
        message += "\ninstall them with " + std::string(kSdkHint);
        return fail(message);
    }
    if (!debug_key && !file_exists(keystore)) return fail("can't find the keystore '" + keystore.string() + "'");
    if (!debug_key && options.ks_pass.empty()) return fail("--keystore needs --ks-pass too (a password, env:VAR or file:path)");
    if (debug_key && !file_exists(keystore) && keytool.empty()) {
        return fail("grove signs the app with the debug key in '" + keystore.string() + "', which doesn't exist yet, and it needs keytool to make it; install a JDK (17 or newer) and put it on PATH or set JAVA_HOME");
    }
    if (platform.api > 0 && platform.api < 24) {
        diag.note(file, span, "android.jar is from API " + std::to_string(platform.api) + ", which is older than Grove expects; install " + std::string("\"platforms;android-35\"") + " for a better build");
    }

    std::string label = android_app_label(project);
    std::string package = doc.declaration("package");
    if (package.empty()) package = default_android_package(document_path);
    const bool has_dex = detail::kFizmoActivityDexSize > 0;
    const std::string activity = has_dex ? "fizmo.android.FizmoActivity" : "android.app.NativeActivity";

    const fs::path cache_root = options.tools.cache_dir.empty() ? base / ".grove-cache" : fs::path(options.tools.cache_dir).parent_path();
    const fs::path work = cache_root / (stem + "-android");
    std::error_code ec;
    fs::create_directories(work, ec);
    if (!dir_exists(work)) return fail("can't make the folder '" + work.string() + "'");
    Runner runner{ work, options.tools.verbose, log };

    std::vector<std::vector<std::string>> modules(abis.size());
    std::size_t module_count = 0;
    for (std::size_t k = 0; k < kits.size() && runnable > 0; ++k) {
        const Kit& kit = kits[k];
        const int api = std::max(min_sdk, kit.api);
        const std::string triple = abi_triple(kit.abi) + std::to_string(api);
        const fs::path cxx = ndk_bin / wrapper_name(triple + "-clang++");
        const fs::path cc = ndk_bin / wrapper_name(triple + "-clang");
        if (!file_exists(cxx) || !file_exists(cc)) return fail("the NDK in '" + ndk_bin.string() + "' has no compiler for " + kit.abi + " at API " + std::to_string(api) + " (" + cxx.filename().string() + "); use a newer NDK");

        std::vector<std::string> missing_files;
        const std::string runtime_libs = join_kit_items(kit, kit.runtime_libs, missing_files);
        const std::string optional_archives = join_kit_items(kit, kit.optional_archives, missing_files);
        const std::string optional_libs = join_kit_items(kit, kit.optional_libs, missing_files);
        if (!missing_files.empty()) {
            std::string message = "the Android kit for " + kit.abi + " in '" + kit.dir.string() + "' is missing:";
            for (const std::string& m : missing_files) message += "\n  " + m;
            message += "\nbuild and install the kit again";
            return fail(message);
        }

        ScriptTools tools = options.tools;
        tools.cxx = cxx.string();
        tools.cc = cc.string();
        tools.compiler_from_walnut = false;
        tools.portable = false;
        tools.cross = true;
        tools.target = kit.abi;
        tools.link_flags = { "-static-libstdc++", "-Wl,-z,max-page-size=16384" };
        tools.walnut_args.clear();
        if (!runtime_libs.empty()) tools.walnut_args.insert(tools.walnut_args.end(), { "--runtime-libs", runtime_libs });
        if (!optional_archives.empty()) tools.walnut_args.insert(tools.walnut_args.end(), { "--optional-archives", optional_archives });
        if (!optional_libs.empty()) tools.walnut_args.insert(tools.walnut_args.end(), { "--optional-libs", optional_libs });
        tools.cache_dir = (cache_root / (stem + "-android-" + kit.abi)).string();
        if (options.tools.verbose && log) *log << "grove: building scripts for " << kit.abi << '\n';

        const ScriptBuild built = build_scripts(project, tools, log);
        if (!built.ok() || !diag.ok()) return false;
        std::vector<std::string>& list = modules[k];
        list.assign(project.scripts.size(), std::string());
        std::size_t count = 0;
        for (const BuiltScript& b : built.scripts) {
            if (!b.runnable || !b.ok) continue;
            if (b.index < list.size()) list[b.index] = b.module_path;
            ++count;
        }
        module_count = count;
    }

    Bundle bundle;
    bundle.set_meta("grove-version", GROVE_VERSION_STRING);
    bundle.set_meta("document", doc_abs.filename().generic_string());
    bundle.set_meta("window", options.window_id);
    std::vector<std::string> seen;
    for (const auto& [path, text] : reads) {
        const fs::path abs = fs::absolute(path).lexically_normal();
        fs::path rel = abs.lexically_relative(base);
        if (rel.empty()) rel = abs.filename();
        const std::string key = rel.generic_string();
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        bundle.add("file", key, text);
        ++result.files;
    }
    for (const ScriptBlock& script : project.scripts) {
        if (script.inline_code || script.source_path.empty()) continue;
        fs::path rel = fs::absolute(script.source_path).lexically_normal().lexically_relative(base);
        if (rel.empty()) rel = fs::path(script.source_path).filename();
        bundle.add("source", rel.generic_string(), std::string());
    }

    AndroidManifestInfo info;
    info.package = package;
    info.label = label;
    info.version_name = doc.declaration("version", "1.0");
    info.version_code = doc.declaration("version-code", "1");
    info.min_sdk = std::to_string(min_sdk);
    info.target_sdk = std::to_string(target_sdk);
    info.orientation = doc.declaration("orientation");
    info.activity = activity;
    info.has_code = has_dex;
    info.platform_api = platform.api;

    const fs::path res = work / "res";
    fs::remove_all(res, ec);
    std::vector<std::string> compiled;
    const std::string icon = doc.declaration("icon");
    if (!icon.empty()) {
        const fs::path icon_path = fs::absolute(resolve_path(project.base_dir, icon));
        std::string ext = icon_path.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::optional<std::string> png = read_binary(icon_path);
        if (!png) return fail("can't read the app icon '" + icon_path.string() + "'");
        if (ext != ".png" || png->size() < 8 || png->compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return fail("the app icon '" + icon + "' has to be a PNG file");
        fs::create_directories(res / "mipmap", ec);
        if (!write_binary(res / "mipmap" / "ic_launcher.png", *png)) return fail("can't write '" + (res / "mipmap" / "ic_launcher.png").string() + "'");
        if (!aapt2.empty()) {
            const fs::path flat_dir = work / "compiled";
            fs::remove_all(flat_dir, ec);
            fs::create_directories(flat_dir, ec);
            std::string out;
            if (runner.run({ aapt2, "compile", (res / "mipmap" / "ic_launcher.png").string(), "-o", flat_dir.string() }, out) != 0) {
                return fail("aapt2 couldn't compile the app icon '" + icon + "':\n" + last_lines(out, 15));
            }
            for (const auto& entry : fs::directory_iterator(flat_dir, ec)) {
                if (entry.path().extension() == ".flat") compiled.push_back(entry.path().string());
            }
            if (compiled.empty()) return fail("aapt2 didn't compile the app icon '" + icon + "'");
        }
        info.icon = true;
    }

    const fs::path manifest = work / "AndroidManifest.xml";
    if (!write_binary(manifest, android_manifest_xml(info))) return fail("can't write '" + manifest.string() + "'");

    const fs::path base_apk = work / "base.apk";
    fs::remove(base_apk, ec);
    std::string out;
    if (!aapt2.empty()) {
        std::vector<std::string> cmd = { aapt2, "link", "-o", base_apk.string(), "-I", platform.jar.string(), "--manifest", manifest.string() };
        cmd.insert(cmd.end(), compiled.begin(), compiled.end());
        cmd.insert(cmd.end(), { "--min-sdk-version", info.min_sdk, "--target-sdk-version", info.target_sdk, "--version-code", info.version_code, "--version-name", info.version_name });
        if (runner.run(cmd, out) != 0 || !file_exists(base_apk)) return fail("aapt2 link failed:\n" + last_lines(out, 20));
    } else {
        std::vector<std::string> cmd = { aapt, "package", "-f", "-M", manifest.string(), "-I", platform.jar.string(), "-F", base_apk.string() };
        if (info.icon) cmd.insert(cmd.end(), { "-S", res.string() });
        cmd.insert(cmd.end(), { "--min-sdk-version", info.min_sdk, "--target-sdk-version", info.target_sdk, "--version-code", info.version_code, "--version-name", info.version_name });
        if (runner.run(cmd, out) != 0 || !file_exists(base_apk)) return fail("aapt package failed:\n" + last_lines(out, 20));
    }

    std::vector<ZipAddition> additions;
    additions.push_back({ "assets/grove/app.bundle", serialize_bundle(bundle) });
    for (std::size_t k = 0; k < kits.size(); ++k) {
        const Kit& kit = kits[k];
        const std::optional<std::string> runtime = read_binary(kit.dir / "libgrove_android.so");
        if (!runtime) return fail("can't read '" + (kit.dir / "libgrove_android.so").string() + "'");
        additions.push_back({ "lib/" + kit.abi + "/libgrove_android.so", *runtime });
        for (std::size_t i = 0; i < modules[k].size(); ++i) {
            if (modules[k][i].empty()) continue;
            const std::optional<std::string> bytes = read_binary(modules[k][i]);
            if (!bytes) return fail("can't read the built script '" + modules[k][i] + "'");
            additions.push_back({ "lib/" + kit.abi + "/" + android_module_name(i), *bytes });
        }
    }
    if (has_dex) additions.push_back({ "classes.dex", std::string(reinterpret_cast<const char*>(detail::kFizmoActivityDex), detail::kFizmoActivityDexSize) });

    const fs::path unaligned = work / "unaligned.apk";
    std::string error;
    if (!append_zip_entries(base_apk.string(), unaligned.string(), additions, error)) return fail(error);

    const fs::path aligned = work / "aligned.apk";
    fs::remove(aligned, ec);
    std::string first;
    if (runner.run({ zipalign, "-P", "16", "-f", "4", unaligned.string(), aligned.string() }, first) != 0 || !file_exists(aligned)) {
        if (options.tools.verbose && log) *log << "grove: this zipalign has no -P, so using -p\n";
        if (runner.run({ zipalign, "-p", "-f", "4", unaligned.string(), aligned.string() }, out) != 0 || !file_exists(aligned)) return fail("zipalign failed:\n" + last_lines(first + "\n" + out, 20));
    }

    if (debug_key && !file_exists(keystore)) {
        fs::create_directories(keystore.parent_path(), ec);
        if (log) *log << "grove: making the debug key " << keystore.string() << '\n';
        if (runner.run({ keytool, "-genkeypair", "-v", "-keystore", keystore.string(), "-storepass", "android", "-alias", "androiddebugkey", "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Android Debug,O=Android,C=US" }, out) != 0 || !file_exists(keystore)) {
            return fail("keytool couldn't make the debug key '" + keystore.string() + "':\n" + last_lines(out, 15));
        }
    }

    const std::string output = fs::absolute(options.output.empty() ? default_android_output(document_path) : options.output).lexically_normal().string();
    if (fs::path(output).has_parent_path()) fs::create_directories(fs::path(output).parent_path(), ec);
    fs::remove(output, ec);
    std::vector<std::string> sign = { apksigner, "sign", "--ks", keystore.string() };
    if (debug_key) {
        sign.insert(sign.end(), { "--ks-pass", "pass:android", "--ks-key-alias", "androiddebugkey", "--key-pass", "pass:android" });
    } else {
        sign.insert(sign.end(), { "--ks-pass", keystore_pass(options.ks_pass) });
        if (!options.key_alias.empty()) sign.insert(sign.end(), { "--ks-key-alias", options.key_alias });
        if (!options.key_pass.empty()) sign.insert(sign.end(), { "--key-pass", keystore_pass(options.key_pass) });
    }
    sign.insert(sign.end(), { "--out", output, aligned.string() });
    if (runner.run(sign, out) != 0 || !file_exists(output)) return fail("apksigner couldn't sign the app:\n" + last_lines(out, 20));

    for (const fs::path& p : { base_apk, unaligned, aligned }) fs::remove(p, ec);

    result.ok = true;
    result.output = output;
    result.bytes = fs::file_size(output, ec);
    result.modules = module_count;
    result.abis = abis;
    result.package = package;
    result.activity = activity;
    result.debug_signed = debug_key;

    if (options.install || options.launch) {
        const std::string adb = find_adb(sdks);
        if (adb.empty()) {
            diag.error(file, span, "the APK is built, but grove can't find adb to install it; install it with sdkmanager \"platform-tools\" or put it on PATH");
            return false;
        }
        if (runner.run({ adb, "install", "-r", output }, out) != 0 || out.find("Failure") != std::string::npos) {
            diag.error(file, span, "the APK is built, but adb couldn't install it:\n" + last_lines(out, 15));
            return false;
        }
        result.installed = true;
        if (options.launch) {
            if (runner.run({ adb, "shell", "am", "start", "-n", package + "/" + activity }, out) != 0 || out.find("Error") != std::string::npos) {
                diag.error(file, span, "the app is installed, but adb couldn't start it:\n" + last_lines(out, 15));
                return false;
            }
            result.launched = true;
        }
    }
    return true;
}

} // namespace

AndroidBuildResult build_android_app(const std::string& document_path, const AndroidBuildOptions& options, Diagnostics& diagnostics, std::ostream* log) {
    AndroidBuildResult result;
    std::vector<std::pair<std::string, std::string>> reads;
    set_file_read_observer([&reads](const std::string& path, const std::string& text) { reads.emplace_back(path, text); });
    Project project = load_project(document_path);
    set_file_read_observer(nullptr);

    if (!project.diagnostics.ok()) {
        diagnostics.append(project.diagnostics);
        return result;
    }
    if (!project.android() && !options.force_target) {
        project.diagnostics.error(project.path, project.document.declaration_span(), "this document isn't an Android app; put <!GROVE type=android> at the top or build with --target android");
        diagnostics.append(project.diagnostics);
        return result;
    }
    if (!package_apk(project, document_path, options, result, reads, log)) result.ok = false;
    diagnostics.append(project.diagnostics);
    return result;
}

} // namespace grove
