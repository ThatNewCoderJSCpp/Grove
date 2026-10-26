#include "grove/markup/events.hpp"
#include "grove/markup/node.hpp"
#include "grove/input/quantity.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>

namespace grove {

std::string normalize_event_type(std::string_view type) {
    std::string out;
    out.reserve(type.size());
    for (char c : type) {
        if (c == '_' || c == '-' || c == ' ') continue;
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

EventRegistry::EventRegistry() {
    m_clock = [] {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    };
}

std::uint64_t EventRegistry::add_listener(const Node* node, std::string_view type, Listener listener) {
    if (!listener) return 0;
    auto entry = std::make_shared<Entry>();
    entry->id = m_next_id++;
    entry->node = node;
    entry->type = normalize_event_type(type);
    entry->listener = std::make_shared<Listener>(std::move(listener));
    m_entries.push_back(entry);
    return entry->id;
}

bool EventRegistry::remove_listener(std::uint64_t id) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if ((*it)->id != id) continue;
        (*it)->removed = true;
        m_entries.erase(it);
        return true;
    }
    return false;
}

void EventRegistry::remove_listeners(const Node* node) {
    for (auto& e : m_entries) {
        if (e->node == node) e->removed = true;
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [node](const std::shared_ptr<Entry>& e) { return e->node == node; }), m_entries.end());
}

std::size_t EventRegistry::listener_count(const Node* node, std::string_view type) const {
    const std::string t = normalize_event_type(type);
    return static_cast<std::size_t>(std::count_if(m_entries.begin(), m_entries.end(), [&](const std::shared_ptr<Entry>& e) { return e->node == node && e->type == t; }));
}

bool EventRegistry::has_listeners(std::string_view type) const {
    const std::string t = normalize_event_type(type);
    return std::any_of(m_entries.begin(), m_entries.end(), [&](const std::shared_ptr<Entry>& e) { return e->type == t; });
}

void EventRegistry::report_error(const std::string& message) const {
    if (m_on_error) m_on_error(message);
    else std::cerr << "grove: " << message << '\n';
}

void EventRegistry::call(const Entry& entry, Event& event) {
    if (entry.removed) return;
    const std::shared_ptr<Listener> listener = entry.listener;
    try {
        (*listener)(event);
    } catch (const std::exception& e) {
        report_error("a '" + event.type + "' listener threw: " + e.what());
    } catch (...) {
        report_error("a '" + event.type + "' listener threw an exception");
    }
}

bool EventRegistry::dispatch(Document& document, Node* target, Event& event) {
    const std::string type = normalize_event_type(event.type);
    if (target && (type == "input" || type == "change")) quantity_event(document, *target, event);
    event.target = target;

    std::vector<Node*> path;
    for (Node* n = target; n; n = n->parent()) {
        path.push_back(n);
        if (!event.bubbles) break;
    }

    m_stack.push_back(&event);
    auto run_for = [&](const Node* node) {
        std::vector<std::shared_ptr<Entry>> matching;
        for (const auto& e : m_entries) {
            if (e->node == node && e->type == type) matching.push_back(e);
        }
        for (const auto& e : matching) {
            call(*e, event);
            if (event.propagation_stopped) break;
        }
    };

    for (Node* node : path) {
        event.current_target = node;
        run_for(node);
        if (event.propagation_stopped) break;
    }
    if (!event.propagation_stopped && (event.bubbles || !target)) {
        event.current_target = nullptr;
        run_for(nullptr);
    }
    event.current_target = nullptr;
    m_stack.pop_back();
    return !event.default_prevented;
}

std::uint64_t EventRegistry::set_timeout(double milliseconds, TimerCallback callback) {
    if (!callback) return 0;
    Timer t;
    t.id = m_next_id++;
    t.due = now() + std::max(0.0, milliseconds);
    t.callback = std::make_shared<TimerCallback>(std::move(callback));
    m_timers.push_back(std::move(t));
    return m_timers.back().id;
}

std::uint64_t EventRegistry::set_interval(double milliseconds, TimerCallback callback) {
    if (!callback) return 0;
    Timer t;
    t.id = m_next_id++;
    t.interval = std::max(1.0, milliseconds);
    t.due = now() + t.interval;
    t.repeat = true;
    t.callback = std::make_shared<TimerCallback>(std::move(callback));
    m_timers.push_back(std::move(t));
    return m_timers.back().id;
}

bool EventRegistry::clear_timer(std::uint64_t id) {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        if (it->id != id) continue;
        m_timers.erase(it);
        return true;
    }
    return false;
}

double EventRegistry::next_timer_due() const {
    double best = std::numeric_limits<double>::infinity();
    for (const Timer& t : m_timers) best = std::min(best, t.due);
    return best;
}

std::size_t EventRegistry::run_due_timers() {
    const double t_now = now();
    std::vector<std::pair<double, std::uint64_t>> due;
    for (const Timer& t : m_timers) {
        if (t.due <= t_now) due.emplace_back(t.due, t.id);
    }
    std::sort(due.begin(), due.end());
    std::size_t ran = 0;
    for (const auto& [when, id] : due) {
        (void)when;
        auto it = std::find_if(m_timers.begin(), m_timers.end(), [id = id](const Timer& t) { return t.id == id; });
        if (it == m_timers.end()) continue;
        const std::shared_ptr<TimerCallback> callback = it->callback;
        if (it->repeat) {
            it->due += it->interval;
            if (it->due <= t_now) it->due = t_now + it->interval;
        } else {
            m_timers.erase(it);
        }
        try {
            (*callback)();
        } catch (const std::exception& e) {
            report_error(std::string("a timer threw: ") + e.what());
        } catch (...) {
            report_error("a timer threw an exception");
        }
        ++ran;
    }
    return ran;
}

void EventRegistry::clear() {
    for (auto& e : m_entries) e->removed = true;
    m_entries.clear();
    m_timers.clear();
}

} // namespace grove
