#ifndef GROVE_ANDROID_PACKAGE_HPP
#define GROVE_ANDROID_PACKAGE_HPP

#include "app.hpp"
#include "project.hpp"
#include "script/builder.hpp"

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace grove {

struct AndroidBuildOptions {
    std::string output;
    std::string window_id;
    std::vector<std::string> abis;
    std::string kit_dir;
    std::string ndk;
    std::string sdk;
    std::string keystore;
    std::string ks_pass;
    std::string key_alias;
    std::string key_pass;
    std::string runtime_lib;
    std::string lib_name;
    std::map<std::string, std::string> declarations;
    ScriptTools tools;
    bool force_target = false;
    bool install = false;
    bool launch = false;
};

struct AndroidBuildResult : AppBuildResult {
    std::vector<std::string> abis;
    std::string package;
    std::string activity;
    bool debug_signed = false;
    bool installed = false;
    bool launched = false;
};

struct AndroidManifestInfo {
    std::string package;
    std::string label;
    std::string version_name = "1.0";
    std::string version_code = "1";
    std::string min_sdk = "26";
    std::string target_sdk = "35";
    std::string orientation;
    std::string activity = "android.app.NativeActivity";
    bool has_code = false;
    bool all_files = false;
    std::string lib_name = "grove_android";
    bool icon = false;
    int platform_api = 0;
};

struct ZipAddition {
    std::string name;
    std::string data;
};

std::string default_android_output(const std::string& document_path);
std::string default_android_package(const std::string& document_path);
std::string android_app_label(const Project& project);
std::string android_manifest_xml(const AndroidManifestInfo& info);
bool append_zip_entries(const std::string& input, const std::string& output, const std::vector<ZipAddition>& additions, std::string& error);
std::uint32_t zip_crc32(const std::string& data);

AndroidBuildResult build_android_app(const std::string& document_path, const AndroidBuildOptions& options, Diagnostics& diagnostics, std::ostream* log = nullptr);

} // namespace grove

#endif // GROVE_ANDROID_PACKAGE_HPP
