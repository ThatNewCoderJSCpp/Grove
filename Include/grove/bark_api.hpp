#ifndef GROVE_BARK_API_HPP
#define GROVE_BARK_API_HPP

#include "api/grove_api.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace grove {

namespace bark_detail {

inline std::string str(const char* s) { return s ? std::string(s) : std::string(); }

struct Provider {
    std::function<std::string()> fn;
    std::string last;
};

inline const char* provider_trampoline(void* user) {
    auto* p = static_cast<Provider*>(user);
    try {
        p->last = p->fn ? p->fn() : std::string();
    } catch (const std::exception& e) {
        grove_log(e.what());
        p->last.clear();
    } catch (...) {
        p->last.clear();
    }
    return p->last.c_str();
}

inline void provider_release(void* user) {
    delete static_cast<Provider*>(user);
}

} // namespace bark_detail

class BarkValues {
public:
    BarkValues() : m_values(grove_bark_values_new(), &grove_bark_values_free) {}
    explicit BarkValues(grove_bark_values* borrowed) : m_values(borrowed, [](grove_bark_values*) {}) {}

    grove_bark_values* handle() const { return m_values.get(); }

    void set(const std::string& name, const std::string& value) { grove_bark_values_set(handle(), name.c_str(), value.c_str()); }
    void set_default(const std::string& name, const std::string& value) { grove_bark_values_set_default(handle(), name.c_str(), value.c_str()); }
    bool has(const std::string& name) const { return grove_bark_values_has(handle(), name.c_str()); }
    std::string get(const std::string& name) const { return bark_detail::str(grove_bark_values_get(handle(), name.c_str())); }
    std::string get_or(const std::string& name, const std::string& fallback) const {
        const char* v = grove_bark_values_get(handle(), name.c_str());
        return v ? std::string(v) : fallback;
    }
    void erase(const std::string& name) { grove_bark_values_erase(handle(), name.c_str()); }
    void provide(const std::string& name, std::function<std::string()> fn) {
        auto* boxed = new bark_detail::Provider{ std::move(fn), std::string() };
        grove_bark_values_provide(handle(), name.c_str(), &bark_detail::provider_trampoline, boxed, &bark_detail::provider_release);
    }
    void set_parent(const BarkValues& parent) {
        m_parent = parent.m_values;
        grove_bark_values_set_parent(handle(), parent.handle());
    }
    std::int64_t count() const { return static_cast<std::int64_t>(grove_bark_values_count(handle())); }
    std::string name_at(std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_values_name(handle(), static_cast<size_t>(index))); }

    std::string interpolate(const std::string& text) const { return bark_detail::str(grove_bark_interpolate(text.c_str(), handle())); }
    static std::string escape_braces(const std::string& text) { return bark_detail::str(grove_bark_escape_braces(text.c_str())); }
    std::int64_t unknown_count(const std::string& text) const { return static_cast<std::int64_t>(grove_bark_unknown_count(text.c_str(), handle())); }
    std::string unknown_at(const std::string& text, std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_unknown(text.c_str(), handle(), static_cast<size_t>(index))); }

private:
    std::shared_ptr<grove_bark_values> m_values;
    std::shared_ptr<grove_bark_values> m_parent;
};

inline std::string bark_interpolate(const std::string& text, const BarkValues& values) {
    return values.interpolate(text);
}

inline std::string bark_escape_braces(const std::string& text) {
    return bark_detail::str(grove_bark_escape_braces(text.c_str()));
}

class BarkFile {
public:
    BarkFile() : BarkFile(grove_bark_parse("", "")) {}
    explicit BarkFile(grove_bark_file* file) : m_file(file, &grove_bark_file_free) {}

    static BarkFile parse(const std::string& text, const std::string& path) { return BarkFile(grove_bark_parse(text.c_str(), path.c_str())); }
    static BarkFile load(const std::string& path) {
        grove_bark_file* file = grove_bark_load(path.c_str());
        BarkFile out = file ? BarkFile(file) : BarkFile();
        out.m_loaded = file != nullptr;
        return out;
    }

    grove_bark_file* handle() const { return m_file.get(); }
    bool loaded() const { return m_loaded; }
    bool ok() const { return m_loaded && grove_bark_error_count(handle()) == 0; }

    std::int64_t error_count() const { return static_cast<std::int64_t>(grove_bark_error_count(handle())); }
    std::string error_at(std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_error(handle(), static_cast<size_t>(index))); }

    std::int64_t rule_count() const { return static_cast<std::int64_t>(grove_bark_rule_count(handle())); }
    std::string rule_name(std::int64_t rule) const { return rule < 0 ? std::string() : bark_detail::str(grove_bark_rule_name(handle(), static_cast<size_t>(rule))); }
    std::int64_t find_rule(const std::string& name) const { return grove_bark_find_rule(handle(), name.c_str()); }
    bool has_rule(const std::string& name) const { return find_rule(name) >= 0; }

    std::int64_t property_count(std::int64_t rule) const { return rule < 0 ? 0 : static_cast<std::int64_t>(grove_bark_property_count(handle(), static_cast<size_t>(rule))); }
    std::string property_name(std::int64_t rule, std::int64_t property) const { return rule < 0 || property < 0 ? std::string() : bark_detail::str(grove_bark_property_name(handle(), static_cast<size_t>(rule), static_cast<size_t>(property))); }
    std::int64_t property_line(std::int64_t rule, std::int64_t property) const { return rule < 0 || property < 0 ? 0 : static_cast<std::int64_t>(grove_bark_property_line(handle(), static_cast<size_t>(rule), static_cast<size_t>(property))); }
    std::string property_text(std::int64_t rule, std::int64_t property) const { return rule < 0 || property < 0 ? std::string() : bark_detail::str(grove_bark_property_text(handle(), static_cast<size_t>(rule), static_cast<size_t>(property))); }
    std::int64_t value_count(std::int64_t rule, std::int64_t property) const { return rule < 0 || property < 0 ? 0 : static_cast<std::int64_t>(grove_bark_value_count(handle(), static_cast<size_t>(rule), static_cast<size_t>(property))); }
    std::string value_at(std::int64_t rule, std::int64_t property, std::int64_t index) const { return rule < 0 || property < 0 || index < 0 ? std::string() : bark_detail::str(grove_bark_value(handle(), static_cast<size_t>(rule), static_cast<size_t>(property), static_cast<size_t>(index))); }

    bool has_setting(std::int64_t rule, const std::string& name) const { return rule >= 0 && grove_bark_setting(handle(), static_cast<size_t>(rule), name.c_str(), nullptr) != nullptr; }
    std::string setting(std::int64_t rule, const std::string& name) const { return rule < 0 ? std::string() : bark_detail::str(grove_bark_setting(handle(), static_cast<size_t>(rule), name.c_str(), nullptr)); }
    std::string setting_with(std::int64_t rule, const std::string& name, const BarkValues& values) const { return rule < 0 ? std::string() : bark_detail::str(grove_bark_setting(handle(), static_cast<size_t>(rule), name.c_str(), values.handle())); }

    std::int64_t variable_count() const { return static_cast<std::int64_t>(grove_bark_variable_count(handle())); }
    std::string variable_name(std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_variable_name(handle(), static_cast<size_t>(index))); }
    std::string variable_value(std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_variable_value(handle(), static_cast<size_t>(index))); }
    void add_variables_to(BarkValues& values) const { grove_bark_add_variables(handle(), values.handle()); }

private:
    std::shared_ptr<grove_bark_file> m_file;
    bool m_loaded = true;
};

namespace bark_detail {

inline grove_bark_call*& current_call() {
    thread_local grove_bark_call* call = nullptr;
    return call;
}

inline int& current_flow() {
    thread_local int flow = GROVE_BARK_NEXT;
    return flow;
}

} // namespace bark_detail

class BarkCall {
public:
    BarkCall() = default;
    explicit BarkCall(grove_bark_call* call) : m_call(call) {}

    static BarkCall current() { return BarkCall(bark_detail::current_call()); }
    bool exists() const { return m_call != nullptr; }

    grove_bark_call* handle() const { return m_call; }
    std::string name() const { return bark_detail::str(grove_bark_call_name(m_call)); }
    std::int64_t arg_count() const { return static_cast<std::int64_t>(grove_bark_call_arg_count(m_call)); }
    std::string arg(std::int64_t index) const { return index < 0 ? std::string() : bark_detail::str(grove_bark_call_arg(m_call, static_cast<size_t>(index))); }
    std::string arg_or(std::int64_t index, const std::string& fallback) const { return index >= 0 && index < arg_count() ? arg(index) : fallback; }
    std::string rest(std::int64_t from) const { return bark_detail::str(grove_bark_call_rest(m_call, from < 0 ? 0 : static_cast<size_t>(from))); }
    std::int64_t line() const { return static_cast<std::int64_t>(grove_bark_call_line(m_call)); }
    BarkValues values() const { return BarkValues(grove_bark_call_values(m_call)); }

    std::int64_t next() const { return mark(GROVE_BARK_NEXT); }
    std::int64_t wait() const { return mark(GROVE_BARK_WAIT); }
    std::int64_t stop() const { return mark(GROVE_BARK_STOP); }
    std::int64_t fail(const std::string& message) const { return mark(grove_bark_call_fail(m_call, message.c_str())); }

private:
    std::int64_t mark(int flow) const {
        if (m_call && m_call == bark_detail::current_call()) bark_detail::current_flow() = flow;
        return flow;
    }

    grove_bark_call* m_call = nullptr;
};

namespace bark_detail {

using Handler = std::function<std::int64_t(BarkCall&)>;

using SimpleHandler = std::function<void()>;

struct CallScope {
    grove_bark_call* call_before;
    int flow_before;
    explicit CallScope(grove_bark_call* call) : call_before(current_call()), flow_before(current_flow()) {
        current_call() = call;
        current_flow() = GROVE_BARK_NEXT;
    }
    ~CallScope() {
        current_call() = call_before;
        current_flow() = flow_before;
    }
};

inline int action_trampoline(grove_bark_call* call, void* user) {
    auto* fn = static_cast<Handler*>(user);
    CallScope scope(call);
    BarkCall c(call);
    try {
        return static_cast<int>((*fn)(c));
    } catch (const std::exception& e) {
        return grove_bark_call_fail(call, e.what());
    } catch (...) {
        return grove_bark_call_fail(call, "the action threw an exception");
    }
}

inline void action_release(void* user) {
    delete static_cast<Handler*>(user);
}

inline int simple_trampoline(grove_bark_call* call, void* user) {
    auto* fn = static_cast<SimpleHandler*>(user);
    CallScope scope(call);
    try {
        if (*fn) (*fn)();
        return current_flow();
    } catch (const std::exception& e) {
        return grove_bark_call_fail(call, e.what());
    } catch (...) {
        return grove_bark_call_fail(call, "the action threw an exception");
    }
}

inline void simple_release(void* user) {
    delete static_cast<SimpleHandler*>(user);
}

} // namespace bark_detail

class BarkActions {
public:
    BarkActions() : m_actions(grove_bark_actions_new(), &grove_bark_actions_free) {}

    grove_bark_actions* handle() const { return m_actions.get(); }

    void add(const std::string& name, std::function<std::int64_t(BarkCall&)> fn) { add(name, 0, -1, std::move(fn)); }
    void add(const std::string& name, std::int64_t min_values, std::int64_t max_values, std::function<std::int64_t(BarkCall&)> fn) { add_with(name, min_values, max_values, 0u, std::move(fn)); }
    void add_raw(const std::string& name, std::int64_t min_values, std::int64_t max_values, std::function<std::int64_t(BarkCall&)> fn) { add_with(name, min_values, max_values, GROVE_BARK_RAW, std::move(fn)); }
    void add_simple(const std::string& name, std::int64_t min_values, std::int64_t max_values, bool raw, std::function<void()> fn) {
        auto* boxed = new bark_detail::SimpleHandler(std::move(fn));
        grove_bark_actions_add(handle(), name.c_str(), count(min_values, 0), count(max_values, GROVE_BARK_ANY), raw ? GROVE_BARK_RAW : 0u, &bark_detail::simple_trampoline, boxed, &bark_detail::simple_release);
    }
    void setting(const std::string& name) { setting(name, 1, 1); }
    void setting(const std::string& name, std::int64_t min_values, std::int64_t max_values) { grove_bark_actions_setting(handle(), name.c_str(), count(min_values, 0), count(max_values, GROVE_BARK_ANY)); }
    void add_standard() { grove_bark_actions_add_standard(handle()); }
    bool has(const std::string& name) const { return grove_bark_actions_has(handle(), name.c_str()); }
    bool remove(const std::string& name) { return grove_bark_actions_remove(handle(), name.c_str()); }

    std::int64_t check_count(const BarkFile& file, std::int64_t rule) const { return rule < 0 ? 0 : static_cast<std::int64_t>(grove_bark_check_count(handle(), file.handle(), static_cast<size_t>(rule))); }
    std::string check_at(const BarkFile& file, std::int64_t rule, std::int64_t index) const { return rule < 0 || index < 0 ? std::string() : bark_detail::str(grove_bark_check(handle(), file.handle(), static_cast<size_t>(rule), static_cast<size_t>(index))); }

private:
    static size_t count(std::int64_t n, size_t fallback) { return n < 0 ? fallback : static_cast<size_t>(n); }

    void add_with(const std::string& name, std::int64_t min_values, std::int64_t max_values, unsigned flags, std::function<std::int64_t(BarkCall&)> fn) {
        auto* boxed = new bark_detail::Handler(std::move(fn));
        grove_bark_actions_add(handle(), name.c_str(), count(min_values, 0), count(max_values, GROVE_BARK_ANY), flags, &bark_detail::action_trampoline, boxed, &bark_detail::action_release);
    }

    std::shared_ptr<grove_bark_actions> m_actions;
};

class BarkRunner {
public:
    static constexpr std::int64_t ready = GROVE_BARK_READY;
    static constexpr std::int64_t waiting_state = GROVE_BARK_WAITING;
    static constexpr std::int64_t done_state = GROVE_BARK_DONE;
    static constexpr std::int64_t stopped_state = GROVE_BARK_STOPPED;
    static constexpr std::int64_t failed_state = GROVE_BARK_FAILED;

    BarkRunner(const BarkActions& actions, const BarkFile& file, std::int64_t rule) : BarkRunner(actions, file, rule, nullptr) {}
    BarkRunner(const BarkActions& actions, const BarkFile& file, std::int64_t rule, const BarkValues& parent) : BarkRunner(actions, file, rule, &parent) {}
    BarkRunner(const BarkActions& actions, const BarkFile& file, const std::string& rule) : BarkRunner(actions, file, file.find_rule(rule), nullptr) {}
    BarkRunner(const BarkActions& actions, const BarkFile& file, const std::string& rule, const BarkValues& parent) : BarkRunner(actions, file, file.find_rule(rule), &parent) {}

    grove_bark_runner* handle() const { return m_runner.get(); }
    bool exists() const { return m_runner != nullptr; }

    std::int64_t run() { return m_runner ? grove_bark_runner_run(handle()) : failed_state; }
    std::int64_t step() { return m_runner ? grove_bark_runner_step(handle()) : failed_state; }
    std::int64_t resume() { return m_runner ? grove_bark_runner_resume(handle()) : failed_state; }
    std::int64_t answer(const std::string& name, const std::string& value) { return m_runner ? grove_bark_runner_answer(handle(), name.c_str(), value.c_str()) : failed_state; }
    void stop() { if (m_runner) grove_bark_runner_stop(handle()); }

    std::int64_t state() const { return m_runner ? grove_bark_runner_state(handle()) : failed_state; }
    bool waiting() const { return state() == waiting_state; }
    bool done() const { return state() == done_state; }
    bool stopped() const { return state() == stopped_state; }
    bool failed() const { return state() == failed_state; }
    bool finished() const { return done() || stopped() || failed(); }
    std::string error() const { return m_runner ? bark_detail::str(grove_bark_runner_error(handle())) : std::string("there is no rule to run"); }
    std::int64_t position() const { return m_runner ? static_cast<std::int64_t>(grove_bark_runner_position(handle())) : 0; }
    std::int64_t size() const { return m_runner ? static_cast<std::int64_t>(grove_bark_runner_size(handle())) : 0; }
    BarkValues values() const { return BarkValues(m_runner ? grove_bark_runner_values(handle()) : nullptr); }

private:
    BarkRunner(const BarkActions& actions, const BarkFile& file, std::int64_t rule, const BarkValues* parent) : m_actions(actions) {
        if (parent) m_parent = std::make_shared<BarkValues>(*parent);
        if (rule < 0) return;
        grove_bark_runner* r = grove_bark_runner_new(actions.handle(), file.handle(), static_cast<size_t>(rule), parent ? parent->handle() : nullptr);
        if (r) m_runner = std::shared_ptr<grove_bark_runner>(r, &grove_bark_runner_free);
    }

    BarkActions m_actions;
    std::shared_ptr<BarkValues> m_parent;
    std::shared_ptr<grove_bark_runner> m_runner;
};

} // namespace grove

#endif // GROVE_BARK_API_HPP
