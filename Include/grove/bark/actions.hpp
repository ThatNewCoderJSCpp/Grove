#ifndef GROVE_BARK_ACTIONS_HPP
#define GROVE_BARK_ACTIONS_HPP

#include "stylesheet.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {
namespace bark {

std::string text_of(const Value& value);
std::vector<std::string> texts_of(const ValueList& values);
std::string joined_text(const ValueList& values);

class Values {
public:
    Values() = default;
    explicit Values(const Values* parent) : m_parent(parent) {}

    void set(const std::string& name, std::string value);
    void set_default(const std::string& name, std::string value);
    void provide(const std::string& name, std::function<std::string()> compute);
    void erase(const std::string& name);
    void add_variables(const std::map<std::string, ValueList>& variables);
    void merge(const std::map<std::string, std::string>& values);

    bool has(const std::string& name) const;
    std::optional<std::string> get(const std::string& name) const;
    std::string get_or(const std::string& name, const std::string& fallback = {}) const;
    std::vector<std::string> names() const;
    std::map<std::string, std::string> resolved() const;

    const Values* parent() const noexcept { return m_parent; }
    void set_parent(const Values* parent) noexcept { m_parent = parent; }

private:
    std::map<std::string, std::string> m_values;
    std::map<std::string, std::function<std::string()>> m_providers;
    mutable std::map<std::string, std::string> m_cache;
    const Values* m_parent = nullptr;
};

using Lookup = std::function<std::optional<std::string>(const std::string&)>;
using Wrap = std::function<std::string(const std::string&)>;

struct Interpolation {
    std::string text;
    std::vector<std::string> unknown;
    bool complete() const noexcept { return unknown.empty(); }
};

Interpolation interpolate_with(std::string_view text, const Lookup& lookup, const Wrap& wrap = {});
std::string interpolate(std::string_view text, const Values& values);
std::string interpolate(std::string_view text, const Values& values, const Wrap& wrap);
std::string interpolate(std::string_view text, const std::map<std::string, std::string>& values);
std::vector<std::string> placeholders(std::string_view text);
std::string escape_braces(std::string_view text);

enum class Flow {
    Next,
    Wait,
    Stop,
    Fail
};

class Runner;

struct ActionCall {
    std::string name;
    std::vector<std::string> args;
    const Declaration* declaration = nullptr;
    Values* values = nullptr;
    Runner* runner = nullptr;
    std::size_t index = 0;
    std::string error;

    std::string arg(std::size_t i, const std::string& fallback = {}) const { return i < args.size() ? args[i] : fallback; }
    std::string rest(std::size_t from) const;
    Flow fail(std::string message) { error = std::move(message); return Flow::Fail; }
};

using ActionHandler = std::function<Flow(ActionCall&)>;

struct ActionInfo {
    std::string name;
    std::string summary;
    std::size_t min_args = 0;
    std::size_t max_args = std::numeric_limits<std::size_t>::max();
    bool interpolate = true;
    bool setting = false;
    bool once = false;
};

struct ActionProblem {
    Span span;
    std::string property;
    std::string message;
};

class Actions {
public:
    Actions& add(ActionInfo info, ActionHandler handler);
    Actions& add(const std::string& name, ActionHandler handler);
    Actions& add(const std::string& name, std::size_t min_args, std::size_t max_args, ActionHandler handler);
    Actions& setting(const std::string& name, std::size_t min_args = 1, std::size_t max_args = 1, std::string summary = {});
    Actions& add_standard();
    bool remove(const std::string& name);

    bool has(const std::string& name) const;
    bool is_setting(const std::string& name) const;
    const ActionInfo* info(const std::string& name) const;
    const ActionHandler* handler(const std::string& name) const;
    std::vector<std::string> names() const;

    std::vector<ActionProblem> check(const Rule& rule) const;
    std::vector<ActionProblem> check(const std::vector<Declaration>& declarations) const;

private:
    struct Entry {
        ActionInfo info;
        ActionHandler handler;
    };
    std::map<std::string, Entry> m_entries;
};

std::optional<std::string> setting_of(const Rule& rule, const std::string& name);
std::optional<std::string> setting_of(const Rule& rule, const std::string& name, const Values& values);
std::vector<const Declaration*> declarations_of(const Rule& rule, const std::string& name);
const Rule* find_rule(const Stylesheet& sheet, const std::string& selector);

class Runner {
public:
    enum class State {
        Ready,
        Waiting,
        Done,
        Stopped,
        Failed
    };

    Runner(const Actions& actions, std::vector<Declaration> declarations, const Values* parent = nullptr);
    Runner(const Actions& actions, const Rule& rule, const Values* parent = nullptr);

    State run();
    State step();
    State resume();
    State answer(const std::string& name, const std::string& value);
    void stop();

    State state() const noexcept { return m_state; }
    bool finished() const noexcept { return m_state == State::Done || m_state == State::Stopped || m_state == State::Failed; }
    bool waiting() const noexcept { return m_state == State::Waiting; }
    std::size_t position() const noexcept { return m_index; }
    std::size_t size() const noexcept { return m_declarations.size(); }
    const std::string& error() const noexcept { return m_error; }
    const Declaration* current() const;

    Values& values() noexcept { return m_values; }
    const Values& values() const noexcept { return m_values; }

private:
    const Actions& m_actions;
    std::vector<Declaration> m_declarations;
    Values m_values;
    std::size_t m_index = 0;
    State m_state = State::Ready;
    std::string m_error;
};

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_ACTIONS_HPP
