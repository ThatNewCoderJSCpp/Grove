#ifndef GROVE_RUNTIME_RUNTIME_HPP
#define GROVE_RUNTIME_RUNTIME_HPP

#include "../project.hpp"
#include "../script/builder.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__ANDROID__)
namespace fizmo {
namespace android {
class App;
} // namespace android
} // namespace fizmo
#endif

namespace grove {
namespace runtime {

struct RunOptions {
    std::string window_id;
    double max_fps = 60.0;
    double idle_fps = 12.0;
    std::optional<bool> vsync;
    bool scripts = true;
    ScriptTools tools;
    const ScriptBuild* prebuilt = nullptr;
};

int run(Project& project, const RunOptions& options = RunOptions());

#if defined(__ANDROID__)
int run_android(Project& project, fizmo::android::App& app, const ScriptBuild* scripts, const RunOptions& options = RunOptions());
#endif

std::vector<std::string> font_families(std::string_view kind = "all");

class HeadlessView {
public:
    explicit HeadlessView(Project& project, const std::string& window_id = std::string());
    ~HeadlessView();
    HeadlessView(const HeadlessView&) = delete;
    HeadlessView& operator=(const HeadlessView&) = delete;

    bool ready() const noexcept;
    bool focus(Node& node);
    std::string edit_text() const;

    bool key(std::string_view name, unsigned modifiers = 0);
    void text(const std::string& text);
    void type(const std::string& text);
    void paste(const std::string& text);

    void set_clipboard(const std::string& text);
    std::string clipboard() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace runtime
} // namespace grove

#endif // GROVE_RUNTIME_RUNTIME_HPP
