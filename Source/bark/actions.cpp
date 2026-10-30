#include "grove/bark/actions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
        check_functions(d.values, d, out);
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

void Actions::check_functions(const ValueList& values, const Declaration& d, std::vector<ActionProblem>& out) const {
    for (const Value& v : values) {
        if (!v.is_function()) continue;
        check_functions(v.args, d, out);
        const FunctionInfo* f = function_info(v.text);
        if (!f) continue;
        const std::size_t n = v.args.size();
        if (n < f->min_args) {
            out.push_back(ActionProblem{ v.span, d.property, v.text + "() needs " + (f->min_args == 1 ? std::string("a value") : "at least " + std::to_string(f->min_args) + " values") });
        } else if (n > f->max_args) {
            out.push_back(ActionProblem{ v.span, d.property, v.text + "()" + (f->max_args == 0 ? std::string(" takes no values") : " takes at most " + std::to_string(f->max_args) + (f->max_args == 1 ? " value" : " values")) });
        }
    }
}

std::string FunctionCall::rest(std::size_t from) const {
    std::string out;
    for (std::size_t i = from; i < args.size(); ++i) {
        if (i > from) out += ' ';
        out += args[i];
    }
    return out;
}

Actions& Actions::function(FunctionInfo info, FunctionHandler handler) {
    const std::string name = info.name;
    m_functions[name] = FunctionEntry{ std::move(info), std::move(handler) };
    return *this;
}

Actions& Actions::function(const std::string& name, FunctionHandler handler) {
    FunctionInfo info;
    info.name = name;
    return function(std::move(info), std::move(handler));
}

Actions& Actions::function(const std::string& name, std::size_t min_args, std::size_t max_args, FunctionHandler handler) {
    FunctionInfo info;
    info.name = name;
    info.min_args = min_args;
    info.max_args = max_args;
    return function(std::move(info), std::move(handler));
}

bool Actions::remove_function(const std::string& name) {
    return m_functions.erase(name) > 0;
}

bool Actions::has_function(const std::string& name) const {
    return m_functions.count(name) > 0;
}

const FunctionInfo* Actions::function_info(const std::string& name) const {
    const auto it = m_functions.find(name);
    return it == m_functions.end() ? nullptr : &it->second.info;
}

std::vector<std::string> Actions::function_names() const {
    std::vector<std::string> out;
    for (const auto& [name, entry] : m_functions) out.push_back(name);
    return out;
}

std::optional<std::string> Actions::evaluate(const Value& value, Values& values, std::string& error, Runner* runner, bool interpolate_text) const {
    const auto it = value.is_function() ? m_functions.find(value.text) : m_functions.end();
    if (it == m_functions.end()) {
        std::string text = text_of(value);
        return interpolate_text ? interpolate(text, values) : text;
    }
    const FunctionEntry& entry = it->second;
    const std::size_t n = value.args.size();
    if (n < entry.info.min_args || n > entry.info.max_args) {
        error = value.text + "() takes " + (entry.info.min_args == entry.info.max_args ? std::to_string(entry.info.min_args) : entry.info.max_args == std::numeric_limits<std::size_t>::max() ? "at least " + std::to_string(entry.info.min_args) : std::to_string(entry.info.min_args) + " to " + std::to_string(entry.info.max_args)) + (entry.info.max_args == 1 && entry.info.min_args == 1 ? " value" : " values") + ", not " + std::to_string(n);
        return std::nullopt;
    }
    FunctionCall call;
    call.name = value.text;
    call.value = &value;
    call.values = &values;
    call.runner = runner;
    for (const Value& a : value.args) {
        std::optional<std::string> t = evaluate(a, values, error, runner, interpolate_text && entry.info.interpolate);
        if (!t) return std::nullopt;
        call.args.push_back(std::move(*t));
    }
    std::string result;
    try {
        result = entry.handler(call);
    } catch (const std::exception& e) {
        call.fail(e.what());
    } catch (...) {
        call.fail(value.text + "() threw an exception");
    }
    if (call.failed) {
        error = call.error.empty() ? value.text + "() failed" : call.error;
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::string>> Actions::evaluate_all(const ValueList& list, Values& names, std::string& error, Runner* runner, bool interpolate_text) const {
    std::vector<std::string> out;
    out.reserve(list.size());
    for (const Value& v : list) {
        std::optional<std::string> t = evaluate(v, names, error, runner, interpolate_text);
        if (!t) return std::nullopt;
        out.push_back(std::move(*t));
    }
    return out;
}

std::optional<std::string> Actions::setting(const Rule& rule, const std::string& name, Values& values, std::string* error) const {
    for (const Declaration& d : rule.declarations) {
        if (d.property != name) continue;
        std::string why;
        const std::optional<std::vector<std::string>> parts = evaluate_all(d.values, values, why);
        if (!parts) {
            if (error) *error = why;
            return std::nullopt;
        }
        std::string out;
        for (std::size_t i = 0; i < parts->size(); ++i) {
            if (i) out += ' ';
            out += (*parts)[i];
        }
        return out;
    }
    return std::nullopt;
}

Actions& Actions::add_standard_functions() {
    auto lower_upper = [](bool upper) {
        return [upper](FunctionCall& call) {
            std::string out = call.rest(0);
            for (char& c : out) c = static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
            return out;
        };
    };
    function("upper", 1, std::numeric_limits<std::size_t>::max(), lower_upper(true));
    function("lower", 1, std::numeric_limits<std::size_t>::max(), lower_upper(false));
    function("trim", 1, 1, [](FunctionCall& call) {
        const std::string& t = call.args[0];
        std::size_t a = 0, b = t.size();
        while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
        return t.substr(a, b - a);
    });
    function("join", 1, std::numeric_limits<std::size_t>::max(), [](FunctionCall& call) {
        std::string out;
        for (std::size_t i = 1; i < call.args.size(); ++i) {
            if (i > 1) out += call.args[0];
            out += call.args[i];
        }
        return out;
    });
    function("concat", 0, std::numeric_limits<std::size_t>::max(), [](FunctionCall& call) {
        std::string out;
        for (const std::string& a : call.args) out += a;
        return out;
    });
    function("replace", 3, 3, [](FunctionCall& call) {
        std::string out = call.args[0];
        const std::string& from = call.args[1];
        if (from.empty()) return out;
        for (std::size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + call.args[2].size())) out.replace(at, from.size(), call.args[2]);
        return out;
    });
    function("length", 1, 1, [](FunctionCall& call) { return std::to_string(call.args[0].size()); });
    function("env", 1, 2, [](FunctionCall& call) {
        const char* v = std::getenv(call.args[0].c_str());
        return v ? std::string(v) : call.arg(1);
    });
    function("or", 1, std::numeric_limits<std::size_t>::max(), [](FunctionCall& call) {
        for (const std::string& a : call.args) {
            if (!a.empty()) return a;
        }
        return std::string();
    });
    return *this;
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
        if (v.is_function() && m_actions.has_function(v.text)) {
            std::string why;
            std::optional<std::string> t = m_actions.evaluate(v, m_values, why, this, info->interpolate);
            if (!t) {
                m_error = d.property + ": " + why;
                m_state = State::Failed;
                return m_state;
            }
            call.args.push_back(std::move(*t));
            continue;
        }
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
