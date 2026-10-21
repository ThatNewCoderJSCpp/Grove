#include "grove/bark/actions.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace grove {
namespace bark {

namespace {

bool name_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
}

struct Placeholder {
    std::size_t end = 0;
    std::string name;
    bool has_fallback = false;
    std::string fallback;
};

std::optional<Placeholder> read_placeholder(std::string_view text, std::size_t open) {
    std::size_t i = open + 1;
    if (i >= text.size() || !name_start(text[i])) return std::nullopt;
    Placeholder p;
    while (i < text.size() && name_char(text[i])) p.name += text[i++];
    if (i < text.size() && text[i] == '}') {
        p.end = i + 1;
        return p;
    }
    if (i < text.size() && text[i] == ':') {
        const std::size_t close = text.find('}', i + 1);
        if (close == std::string_view::npos) return std::nullopt;
        const std::string_view inside = text.substr(i + 1, close - i - 1);
        if (inside.find('{') != std::string_view::npos) return std::nullopt;
        p.has_fallback = true;
        p.fallback = std::string(inside);
        p.end = close + 1;
        return p;
    }
    return std::nullopt;
}

} // namespace

std::string text_of(const Value& value) {
    if (value.is_string() || value.is_ident()) return value.text;
    return to_string(value);
}

std::vector<std::string> texts_of(const ValueList& values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const Value& v : values) out.push_back(text_of(v));
    return out;
}

std::string joined_text(const ValueList& values) {
    if (values.size() == 1) return text_of(values.front());
    std::string out;
    for (const Value& v : values) {
        std::string t = text_of(v);
        if (v.is_string() && (t.empty() || t.find_first_of(" \t\"'") != std::string::npos)) {
            std::string q = "\"";
            for (char c : t) {
                if (c == '"' || c == '\\') q += '\\';
                q += c;
            }
            t = q + "\"";
        }
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

void Values::set(const std::string& name, std::string value) {
    m_values[name] = std::move(value);
}

void Values::set_default(const std::string& name, std::string value) {
    if (!has(name)) m_values[name] = std::move(value);
}

void Values::provide(const std::string& name, std::function<std::string()> compute) {
    m_providers[name] = std::move(compute);
    m_cache.erase(name);
}

void Values::erase(const std::string& name) {
    m_values.erase(name);
    m_providers.erase(name);
    m_cache.erase(name);
}

void Values::add_variables(const std::map<std::string, ValueList>& variables) {
    for (const auto& [name, list] : variables) set_default(name, joined_text(list));
}

void Values::merge(const std::map<std::string, std::string>& values) {
    for (const auto& [name, value] : values) m_values[name] = value;
}

bool Values::has(const std::string& name) const {
    if (m_values.count(name) || m_providers.count(name)) return true;
    return m_parent && m_parent->has(name);
}

std::optional<std::string> Values::get(const std::string& name) const {
    if (const auto it = m_values.find(name); it != m_values.end()) return it->second;
    if (const auto it = m_providers.find(name); it != m_providers.end()) {
        if (const auto cached = m_cache.find(name); cached != m_cache.end()) return cached->second;
        std::string value = it->second ? it->second() : std::string();
        m_cache[name] = value;
        return value;
    }
    if (m_parent) return m_parent->get(name);
    return std::nullopt;
}

std::string Values::get_or(const std::string& name, const std::string& fallback) const {
    return get(name).value_or(fallback);
}

std::vector<std::string> Values::names() const {
    std::set<std::string> all;
    if (m_parent) {
        for (const std::string& n : m_parent->names()) all.insert(n);
    }
    for (const auto& [name, value] : m_providers) all.insert(name);
    for (const auto& [name, value] : m_values) all.insert(name);
    return std::vector<std::string>(all.begin(), all.end());
}

std::map<std::string, std::string> Values::resolved() const {
    std::map<std::string, std::string> out;
    for (const std::string& name : names()) out[name] = get_or(name);
    return out;
}

Interpolation interpolate_with(std::string_view text, const Lookup& lookup, const Wrap& wrap) {
    Interpolation result;
    std::string& out = result.text;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') {
            out += '{';
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') {
            out += '}';
            i += 2;
            continue;
        }
        if (c == '{' && (i == 0 || text[i - 1] != '$')) {
            if (const std::optional<Placeholder> p = read_placeholder(text, i)) {
                std::optional<std::string> value = lookup ? lookup(p->name) : std::nullopt;
                if (!value && p->has_fallback) value = p->fallback;
                if (value) {
                    out += wrap ? wrap(*value) : *value;
                } else {
                    out.append(text.substr(i, p->end - i));
                    if (std::find(result.unknown.begin(), result.unknown.end(), p->name) == result.unknown.end()) result.unknown.push_back(p->name);
                }
                i = p->end;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return result;
}

std::string interpolate(std::string_view text, const Values& values) {
    return interpolate_with(text, [&values](const std::string& name) { return values.get(name); }).text;
}

std::string interpolate(std::string_view text, const Values& values, const Wrap& wrap) {
    return interpolate_with(text, [&values](const std::string& name) { return values.get(name); }, wrap).text;
}

std::string interpolate(std::string_view text, const std::map<std::string, std::string>& values) {
    return interpolate_with(text, [&values](const std::string& name) -> std::optional<std::string> {
        const auto it = values.find(name);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }).text;
}

std::vector<std::string> placeholders(std::string_view text) {
    std::vector<std::string> names;
    interpolate_with(text, [&names](const std::string& name) -> std::optional<std::string> {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        return std::nullopt;
    });
    return names;
}

std::string escape_braces(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '{' || c == '}') out += c;
        out += c;
    }
    return out;
}

std::string ActionCall::rest(std::size_t from) const {
    std::string out;
    for (std::size_t i = from; i < args.size(); ++i) {
        if (i > from) out += ' ';
        out += args[i];
    }
    return out;
}

Actions& Actions::add(ActionInfo info, ActionHandler handler) {
    const std::string name = info.name;
    m_entries[name] = Entry{ std::move(info), std::move(handler) };
    return *this;
}

Actions& Actions::add(const std::string& name, ActionHandler handler) {
    ActionInfo info;
    info.name = name;
    return add(std::move(info), std::move(handler));
}

Actions& Actions::add(const std::string& name, std::size_t min_args, std::size_t max_args, ActionHandler handler) {
    ActionInfo info;
    info.name = name;
    info.min_args = min_args;
    info.max_args = max_args;
    return add(std::move(info), std::move(handler));
}

Actions& Actions::setting(const std::string& name, std::size_t min_args, std::size_t max_args, std::string summary) {
    ActionInfo info;
    info.name = name;
    info.summary = std::move(summary);
    info.min_args = min_args;
    info.max_args = max_args;
    info.setting = true;
    info.once = true;
    return add(std::move(info), ActionHandler{});
}

Actions& Actions::add_standard() {
    ActionInfo set;
    set.name = "set";
    set.summary = "set: name value; gives a value a name for the steps after it";
    set.min_args = 1;
    add(set, [](ActionCall& call) {
        call.values->set(call.arg(0), call.rest(1));
        return Flow::Next;
    });
    ActionInfo fallback;
    fallback.name = "default";
    fallback.summary = "default: name value; like set, but only when the name has no value yet";
    fallback.min_args = 1;
    add(fallback, [](ActionCall& call) {
        call.values->set_default(call.arg(0), call.rest(1));
        return Flow::Next;
    });
    ActionInfo stop;
    stop.name = "stop";
    stop.summary = "stop; ends the rule here";
    stop.max_args = 0;
    add(stop, [](ActionCall&) { return Flow::Stop; });
    ActionInfo fail;
    fail.name = "fail";
    fail.summary = "fail: \"why\"; ends the rule with an error";
    add(fail, [](ActionCall& call) { return call.fail(call.args.empty() ? std::string("failed") : call.rest(0)); });
    return *this;
}

bool Actions::remove(const std::string& name) {
    return m_entries.erase(name) > 0;
}

bool Actions::has(const std::string& name) const {
    return m_entries.count(name) > 0;
}

bool Actions::is_setting(const std::string& name) const {
    const ActionInfo* i = info(name);
    return i && i->setting;
}

const ActionInfo* Actions::info(const std::string& name) const {
    const auto it = m_entries.find(name);
    return it == m_entries.end() ? nullptr : &it->second.info;
}

const ActionHandler* Actions::handler(const std::string& name) const {
    const auto it = m_entries.find(name);
    if (it == m_entries.end() || !it->second.handler) return nullptr;
    return &it->second.handler;
}

std::vector<std::string> Actions::names() const {
    std::vector<std::string> out;
    for (const auto& [name, entry] : m_entries) out.push_back(name);
    return out;
}

std::vector<ActionProblem> Actions::check(const Rule& rule) const {
    return check(rule.declarations);
}

std::vector<ActionProblem> Actions::check(const std::vector<Declaration>& declarations) const {
    std::vector<ActionProblem> out;
    std::set<std::string> seen;
    for (const Declaration& d : declarations) {
        const ActionInfo* i = info(d.property);
        if (!i) {
            out.push_back(ActionProblem{ d.span, d.property, "unknown property '" + d.property + "'" });
            continue;
        }
        const std::size_t n = d.values.size();
        if (n < i->min_args) {
            out.push_back(ActionProblem{ d.span, d.property, d.property + " needs " + (i->min_args == 1 ? std::string("a value") : "at least " + std::to_string(i->min_args) + " values") });
        } else if (n > i->max_args) {
            out.push_back(ActionProblem{ d.span, d.property, d.property + (i->max_args == 0 ? std::string(" takes no values") : " takes at most " + std::to_string(i->max_args) + (i->max_args == 1 ? " value" : " values")) });
        }
        if (i->once && !seen.insert(d.property).second) out.push_back(ActionProblem{ d.span, d.property, d.property + " is set more than once; the first one is used" });
    }
    return out;
}

std::optional<std::string> setting_of(const Rule& rule, const std::string& name) {
    for (const Declaration& d : rule.declarations) {
        if (d.property == name) return joined_text(d.values);
    }
    return std::nullopt;
}

std::optional<std::string> setting_of(const Rule& rule, const std::string& name, const Values& values) {
    const std::optional<std::string> raw = setting_of(rule, name);
    if (!raw) return std::nullopt;
    return interpolate(*raw, values);
}

std::vector<const Declaration*> declarations_of(const Rule& rule, const std::string& name) {
    std::vector<const Declaration*> out;
    for (const Declaration& d : rule.declarations) {
        if (d.property == name) out.push_back(&d);
    }
    return out;
}

const Rule* find_rule(const Stylesheet& sheet, const std::string& selector) {
    for (const Rule& rule : sheet.rules()) {
        for (const Selector& s : rule.selectors) {
            if (s.to_string() == selector) return &rule;
        }
    }
    return nullptr;
}

Runner::Runner(const Actions& actions, std::vector<Declaration> declarations, const Values* parent)
    : m_actions(actions), m_declarations(std::move(declarations)), m_values(parent) {}

Runner::Runner(const Actions& actions, const Rule& rule, const Values* parent)
    : Runner(actions, rule.declarations, parent) {}

const Declaration* Runner::current() const {
    return m_index < m_declarations.size() ? &m_declarations[m_index] : nullptr;
}

Runner::State Runner::step() {
    if (finished() || m_state == State::Waiting) return m_state;
    while (m_index < m_declarations.size() && m_actions.is_setting(m_declarations[m_index].property)) ++m_index;
    if (m_index >= m_declarations.size()) {
        m_state = State::Done;
        return m_state;
    }
    const Declaration& d = m_declarations[m_index];
    const ActionInfo* info = m_actions.info(d.property);
    const ActionHandler* handler = m_actions.handler(d.property);
    if (!info || !handler) {
        m_error = "unknown action '" + d.property + "'";
        m_state = State::Failed;
        return m_state;
    }
    ActionCall call;
    call.name = d.property;
    call.declaration = &d;
    call.values = &m_values;
    call.runner = this;
    call.index = m_index;
    for (const Value& v : d.values) {
        std::string text = text_of(v);
        call.args.push_back(info->interpolate ? interpolate(text, m_values) : std::move(text));
    }
    ++m_index;
    const Flow flow = (*handler)(call);
    switch (flow) {
    case Flow::Next:
        m_state = m_index >= m_declarations.size() ? State::Done : State::Ready;
        break;
    case Flow::Wait:
        m_state = State::Waiting;
        break;
    case Flow::Stop:
        m_state = State::Stopped;
        break;
    case Flow::Fail:
        m_error = call.error.empty() ? d.property + " failed" : call.error;
        m_state = State::Failed;
        break;
    }
    return m_state;
}

Runner::State Runner::run() {
    while (m_state == State::Ready) step();
    return m_state;
}

Runner::State Runner::resume() {
    if (m_state != State::Waiting) return m_state;
    m_state = m_index >= m_declarations.size() ? State::Done : State::Ready;
    return run();
}

Runner::State Runner::answer(const std::string& name, const std::string& value) {
    m_values.set(name, value);
    return resume();
}

void Runner::stop() {
    if (!finished()) m_state = State::Stopped;
}

} // namespace bark
} // namespace grove
