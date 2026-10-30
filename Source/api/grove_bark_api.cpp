#define GROVE_API_BUILDING
#include "grove/api/grove_api.h"
#include "grove/bark/actions.hpp"
#include "grove/bark/parser.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct grove_bark_values {
    grove::bark::Values own;
    grove::bark::Values* use = nullptr;
};

struct grove_bark_file {
    grove::bark::Stylesheet sheet;
    std::vector<std::string> errors;
};

struct grove_bark_actions {
    std::shared_ptr<grove::bark::Actions> actions = std::make_shared<grove::bark::Actions>();
};

struct grove_bark_runner {
    std::shared_ptr<grove::bark::Actions> actions;
    std::unique_ptr<grove::bark::Runner> runner;
    grove_bark_values view;
};

struct grove_bark_call {
    grove::bark::ActionCall* call = nullptr;
    grove::bark::FunctionCall* fcall = nullptr;
    grove_bark_values view;
    grove_bark_runner* runner = nullptr;
};

namespace {

const char* keep(std::string value) {
    thread_local std::array<std::string, 16> ring;
    thread_local std::size_t next = 0;
    std::string& slot = ring[next];
    next = (next + 1) % ring.size();
    slot = std::move(value);
    return slot.c_str();
}

std::string text(const char* s) {
    return s ? std::string(s) : std::string();
}

struct Release {
    void* user_data = nullptr;
    grove_release_fn release = nullptr;
    ~Release() {
        if (release) release(user_data);
    }
};

grove::bark::Values* values_of(grove_bark_values* v) {
    return v ? v->use : nullptr;
}

const grove::bark::Values* values_of(const grove_bark_values* v) {
    return v ? v->use : nullptr;
}

const grove::bark::Rule* rule_of(const grove_bark_file* file, std::size_t rule) {
    if (!file || rule >= file->sheet.rules().size()) return nullptr;
    return &file->sheet.rules()[rule];
}

const grove::bark::Declaration* declaration_of(const grove_bark_file* file, std::size_t rule, std::size_t property) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!r || property >= r->declarations.size()) return nullptr;
    return &r->declarations[property];
}

grove::bark::Interpolation interpolation(const char* text_in, const grove_bark_values* values) {
    const grove::bark::Values* v = values_of(values);
    return grove::bark::interpolate_with(text(text_in), [v](const std::string& name) -> std::optional<std::string> {
        if (!v) return std::nullopt;
        return v->get(name);
    });
}

grove::bark::Flow flow_of(int code) {
    switch (code) {
    case GROVE_BARK_WAIT: return grove::bark::Flow::Wait;
    case GROVE_BARK_STOP: return grove::bark::Flow::Stop;
    case GROVE_BARK_FAIL: return grove::bark::Flow::Fail;
    default: return grove::bark::Flow::Next;
    }
}

int state_of(grove::bark::Runner::State state) {
    switch (state) {
    case grove::bark::Runner::State::Ready: return GROVE_BARK_READY;
    case grove::bark::Runner::State::Waiting: return GROVE_BARK_WAITING;
    case grove::bark::Runner::State::Done: return GROVE_BARK_DONE;
    case grove::bark::Runner::State::Stopped: return GROVE_BARK_STOPPED;
    case grove::bark::Runner::State::Failed: return GROVE_BARK_FAILED;
    }
    return GROVE_BARK_FAILED;
}

thread_local grove_bark_runner* g_running = nullptr;
thread_local std::string g_evaluate_error;

struct RunningScope {
    grove_bark_runner* before;
    explicit RunningScope(grove_bark_runner* r) : before(g_running) { g_running = r; }
    ~RunningScope() { g_running = before; }
};

} // namespace

extern "C" {

grove_bark_values* grove_bark_values_new(void) {
    auto* v = new grove_bark_values();
    v->use = &v->own;
    return v;
}

void grove_bark_values_free(grove_bark_values* values) {
    delete values;
}

void grove_bark_values_set(grove_bark_values* values, const char* name, const char* value) {
    if (grove::bark::Values* v = values_of(values); v && name) v->set(name, text(value));
}

void grove_bark_values_set_default(grove_bark_values* values, const char* name, const char* value) {
    if (grove::bark::Values* v = values_of(values); v && name) v->set_default(name, text(value));
}

bool grove_bark_values_has(const grove_bark_values* values, const char* name) {
    const grove::bark::Values* v = values_of(values);
    return v && name && v->has(name);
}

const char* grove_bark_values_get(const grove_bark_values* values, const char* name) {
    const grove::bark::Values* v = values_of(values);
    if (!v || !name) return nullptr;
    const std::optional<std::string> got = v->get(name);
    return got ? keep(*got) : nullptr;
}

void grove_bark_values_erase(grove_bark_values* values, const char* name) {
    if (grove::bark::Values* v = values_of(values); v && name) v->erase(name);
}

void grove_bark_values_provide(grove_bark_values* values, const char* name, grove_bark_provider_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    grove::bark::Values* v = values_of(values);
    if (!v || !name || !fn) return;
    v->provide(name, [fn, guard]() { return text(fn(guard->user_data)); });
}

void grove_bark_values_set_parent(grove_bark_values* values, const grove_bark_values* parent) {
    if (grove::bark::Values* v = values_of(values)) v->set_parent(values_of(parent));
}

size_t grove_bark_values_count(const grove_bark_values* values) {
    const grove::bark::Values* v = values_of(values);
    return v ? v->names().size() : 0;
}

const char* grove_bark_values_name(const grove_bark_values* values, size_t index) {
    const grove::bark::Values* v = values_of(values);
    if (!v) return nullptr;
    const std::vector<std::string> names = v->names();
    return index < names.size() ? keep(names[index]) : nullptr;
}

const char* grove_bark_interpolate(const char* text_in, const grove_bark_values* values) {
    return keep(interpolation(text_in, values).text);
}

size_t grove_bark_unknown_count(const char* text_in, const grove_bark_values* values) {
    return interpolation(text_in, values).unknown.size();
}

const char* grove_bark_unknown(const char* text_in, const grove_bark_values* values, size_t index) {
    const grove::bark::Interpolation result = interpolation(text_in, values);
    return index < result.unknown.size() ? keep(result.unknown[index]) : nullptr;
}

const char* grove_bark_escape_braces(const char* text_in) {
    return keep(grove::bark::escape_braces(text(text_in)));
}

grove_bark_file* grove_bark_parse(const char* text_in, const char* path) {
    auto* file = new grove_bark_file();
    grove::Diagnostics diagnostics;
    const std::string name = path && *path ? std::string(path) : std::string("<bark>");
    file->sheet = grove::bark::parse_stylesheet(text(text_in), name, diagnostics);
    for (const grove::Diagnostic& d : diagnostics.items()) {
        if (d.severity != grove::Severity::Error) continue;
        file->errors.push_back(name + ":" + std::to_string(d.span.begin.line) + ": " + d.message);
    }
    return file;
}

grove_bark_file* grove_bark_load(const char* path) {
    if (!path) return nullptr;
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    std::ostringstream ss;
    ss << in.rdbuf();
    return grove_bark_parse(ss.str().c_str(), path);
}

void grove_bark_file_free(grove_bark_file* file) {
    delete file;
}

size_t grove_bark_error_count(const grove_bark_file* file) {
    return file ? file->errors.size() : 0;
}

const char* grove_bark_error(const grove_bark_file* file, size_t index) {
    if (!file || index >= file->errors.size()) return nullptr;
    return file->errors[index].c_str();
}

size_t grove_bark_rule_count(const grove_bark_file* file) {
    return file ? file->sheet.rules().size() : 0;
}

const char* grove_bark_rule_name(const grove_bark_file* file, size_t rule) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!r) return nullptr;
    std::string out;
    for (const grove::bark::Selector& s : r->selectors) {
        if (!out.empty()) out += ", ";
        out += s.to_string();
    }
    return keep(out);
}

int64_t grove_bark_find_rule(const grove_bark_file* file, const char* name) {
    if (!file || !name) return -1;
    const std::vector<grove::bark::Rule>& rules = file->sheet.rules();
    for (std::size_t i = 0; i < rules.size(); ++i) {
        for (const grove::bark::Selector& s : rules[i].selectors) {
            if (s.to_string() == name) return static_cast<int64_t>(i);
        }
    }
    return -1;
}

size_t grove_bark_property_count(const grove_bark_file* file, size_t rule) {
    const grove::bark::Rule* r = rule_of(file, rule);
    return r ? r->declarations.size() : 0;
}

const char* grove_bark_property_name(const grove_bark_file* file, size_t rule, size_t property) {
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    return d ? d->property.c_str() : nullptr;
}

size_t grove_bark_property_line(const grove_bark_file* file, size_t rule, size_t property) {
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    return d ? static_cast<size_t>(d->span.begin.line) : 0;
}

const char* grove_bark_property_text(const grove_bark_file* file, size_t rule, size_t property) {
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    return d ? keep(grove::bark::joined_text(d->values)) : nullptr;
}

size_t grove_bark_value_count(const grove_bark_file* file, size_t rule, size_t property) {
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    return d ? d->values.size() : 0;
}

const char* grove_bark_value(const grove_bark_file* file, size_t rule, size_t property, size_t index) {
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    if (!d || index >= d->values.size()) return nullptr;
    return keep(grove::bark::text_of(d->values[index]));
}

const char* grove_bark_setting(const grove_bark_file* file, size_t rule, const char* name, const grove_bark_values* values) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!r || !name) return nullptr;
    const std::optional<std::string> raw = grove::bark::setting_of(*r, name);
    if (!raw) return nullptr;
    const grove::bark::Values* v = values_of(values);
    return keep(v ? grove::bark::interpolate(*raw, *v) : *raw);
}

size_t grove_bark_variable_count(const grove_bark_file* file) {
    return file ? file->sheet.variables().size() : 0;
}

const char* grove_bark_variable_name(const grove_bark_file* file, size_t index) {
    if (!file || index >= file->sheet.variables().size()) return nullptr;
    auto it = file->sheet.variables().begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    return it->first.c_str();
}

const char* grove_bark_variable_value(const grove_bark_file* file, size_t index) {
    if (!file || index >= file->sheet.variables().size()) return nullptr;
    auto it = file->sheet.variables().begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    return keep(grove::bark::joined_text(it->second));
}

void grove_bark_add_variables(const grove_bark_file* file, grove_bark_values* values) {
    grove::bark::Values* v = values_of(values);
    if (file && v) v->add_variables(file->sheet.variables());
}

grove_bark_actions* grove_bark_actions_new(void) {
    return new grove_bark_actions();
}

void grove_bark_actions_free(grove_bark_actions* actions) {
    delete actions;
}

void grove_bark_actions_add(grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_action_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    if (!actions || !name || !fn) return;
    grove::bark::ActionInfo info;
    info.name = name;
    info.min_args = min_values;
    info.max_args = max_values;
    info.interpolate = (flags & GROVE_BARK_RAW) == 0;
    actions->actions->add(info, [fn, guard](grove::bark::ActionCall& call) {
        grove_bark_call c;
        c.call = &call;
        c.view.use = call.values;
        c.runner = g_running;
        const int code = fn(&c, guard->user_data);
        return flow_of(code);
    });
}

void grove_bark_actions_setting(grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values) {
    if (actions && name) actions->actions->setting(name, min_values, max_values);
}

void grove_bark_actions_add_standard(grove_bark_actions* actions) {
    if (actions) actions->actions->add_standard();
}

bool grove_bark_actions_has(const grove_bark_actions* actions, const char* name) {
    return actions && name && actions->actions->has(name);
}

bool grove_bark_actions_remove(grove_bark_actions* actions, const char* name) {
    return actions && name && actions->actions->remove(name);
}

size_t grove_bark_check_count(const grove_bark_actions* actions, const grove_bark_file* file, size_t rule) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!actions || !r) return 0;
    return actions->actions->check(*r).size();
}

const char* grove_bark_check(const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t index) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!actions || !r) return nullptr;
    const std::vector<grove::bark::ActionProblem> problems = actions->actions->check(*r);
    if (index >= problems.size()) return nullptr;
    return keep("line " + std::to_string(problems[index].span.begin.line) + ": " + problems[index].message);
}

const char* grove_bark_call_name(const grove_bark_call* call) {
    if (call && call->fcall) return call->fcall->name.c_str();
    return call && call->call ? call->call->name.c_str() : nullptr;
}

size_t grove_bark_call_arg_count(const grove_bark_call* call) {
    if (call && call->fcall) return call->fcall->args.size();
    return call && call->call ? call->call->args.size() : 0;
}

const char* grove_bark_call_arg(const grove_bark_call* call, size_t index) {
    if (call && call->fcall) return index < call->fcall->args.size() ? call->fcall->args[index].c_str() : nullptr;
    if (!call || !call->call || index >= call->call->args.size()) return nullptr;
    return call->call->args[index].c_str();
}

const char* grove_bark_call_rest(const grove_bark_call* call, size_t from) {
    if (call && call->fcall) return keep(call->fcall->rest(from));
    return call && call->call ? keep(call->call->rest(from)) : nullptr;
}

size_t grove_bark_call_line(const grove_bark_call* call) {
    if (call && call->fcall) return call->fcall->value ? static_cast<size_t>(call->fcall->value->span.begin.line) : 0;
    return call && call->call && call->call->declaration ? static_cast<size_t>(call->call->declaration->span.begin.line) : 0;
}

grove_bark_values* grove_bark_call_values(grove_bark_call* call) {
    return call ? &call->view : nullptr;
}

grove_bark_runner* grove_bark_call_runner(grove_bark_call* call) {
    return call ? call->runner : nullptr;
}

int grove_bark_call_fail(grove_bark_call* call, const char* message) {
    if (call && call->fcall) call->fcall->fail(text(message));
    else if (call && call->call) call->call->error = text(message);
    return GROVE_BARK_FAIL;
}

void grove_bark_actions_add_function(grove_bark_actions* actions, const char* name, size_t min_values, size_t max_values, unsigned flags, grove_bark_function_fn fn, void* user_data, grove_release_fn release) {
    auto guard = std::make_shared<Release>();
    guard->user_data = user_data;
    guard->release = release;
    if (!actions || !name || !fn) return;
    grove::bark::FunctionInfo info;
    info.name = name;
    info.min_args = min_values;
    info.max_args = max_values;
    info.interpolate = (flags & GROVE_BARK_RAW) == 0;
    actions->actions->function(info, [fn, guard](grove::bark::FunctionCall& call) {
        grove_bark_call c;
        c.fcall = &call;
        c.view.use = call.values;
        c.runner = g_running;
        const char* result = fn(&c, guard->user_data);
        return result ? std::string(result) : std::string();
    });
}

void grove_bark_actions_add_standard_functions(grove_bark_actions* actions) {
    if (actions) actions->actions->add_standard_functions();
}

bool grove_bark_actions_has_function(const grove_bark_actions* actions, const char* name) {
    return actions && name && actions->actions->has_function(name);
}

bool grove_bark_actions_remove_function(grove_bark_actions* actions, const char* name) {
    return actions && name && actions->actions->remove_function(name);
}

const char* grove_bark_evaluate_setting(const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const char* name, grove_bark_values* values) {
    g_evaluate_error.clear();
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!actions || !r || !name) return nullptr;
    grove::bark::Values scratch;
    grove::bark::Values* v = values_of(values);
    std::string why;
    const std::optional<std::string> out = actions->actions->setting(*r, name, v ? *v : scratch, &why);
    if (!out) {
        g_evaluate_error = why;
        return nullptr;
    }
    return keep(*out);
}

const char* grove_bark_evaluate_value(const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, size_t property, size_t index, grove_bark_values* values) {
    g_evaluate_error.clear();
    const grove::bark::Declaration* d = declaration_of(file, rule, property);
    if (!actions || !d || index >= d->values.size()) return nullptr;
    grove::bark::Values scratch;
    grove::bark::Values* v = values_of(values);
    std::string why;
    const std::optional<std::string> out = actions->actions->evaluate(d->values[index], v ? *v : scratch, why);
    if (!out) {
        g_evaluate_error = why;
        return nullptr;
    }
    return keep(*out);
}

const char* grove_bark_evaluate_error(void) {
    return g_evaluate_error.c_str();
}

grove_bark_runner* grove_bark_runner_new(const grove_bark_actions* actions, const grove_bark_file* file, size_t rule, const grove_bark_values* parent) {
    const grove::bark::Rule* r = rule_of(file, rule);
    if (!actions || !r) return nullptr;
    auto* runner = new grove_bark_runner();
    runner->actions = actions->actions;
    runner->runner = std::make_unique<grove::bark::Runner>(*runner->actions, *r, values_of(parent));
    runner->view.use = &runner->runner->values();
    return runner;
}

void grove_bark_runner_free(grove_bark_runner* runner) {
    delete runner;
}

int grove_bark_runner_run(grove_bark_runner* runner) {
    if (!runner) return GROVE_BARK_FAILED;
    RunningScope scope(runner);
    return state_of(runner->runner->run());
}

int grove_bark_runner_step(grove_bark_runner* runner) {
    if (!runner) return GROVE_BARK_FAILED;
    RunningScope scope(runner);
    return state_of(runner->runner->step());
}

int grove_bark_runner_resume(grove_bark_runner* runner) {
    if (!runner) return GROVE_BARK_FAILED;
    RunningScope scope(runner);
    return state_of(runner->runner->resume());
}

int grove_bark_runner_answer(grove_bark_runner* runner, const char* name, const char* value) {
    if (!runner) return GROVE_BARK_FAILED;
    RunningScope scope(runner);
    return state_of(runner->runner->answer(text(name), text(value)));
}

void grove_bark_runner_stop(grove_bark_runner* runner) {
    if (runner) runner->runner->stop();
}

int grove_bark_runner_state(const grove_bark_runner* runner) {
    return runner ? state_of(runner->runner->state()) : GROVE_BARK_FAILED;
}

const char* grove_bark_runner_error(const grove_bark_runner* runner) {
    return runner ? runner->runner->error().c_str() : nullptr;
}

grove_bark_values* grove_bark_runner_values(grove_bark_runner* runner) {
    return runner ? &runner->view : nullptr;
}

size_t grove_bark_runner_position(const grove_bark_runner* runner) {
    return runner ? runner->runner->position() : 0;
}

size_t grove_bark_runner_size(const grove_bark_runner* runner) {
    return runner ? runner->runner->size() : 0;
}

}
