#ifndef GROVE_MARKUP_EVENTS_HPP
#define GROVE_MARKUP_EVENTS_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

class Node;
class Document;

enum EventModifier : unsigned {
    ModifierShift = 1u,
    ModifierControl = 2u,
    ModifierAlt = 4u,
    ModifierMeta = 8u
};

struct Event {
    std::string type;
    Node* target = nullptr;
    Node* current_target = nullptr;
    double x = 0.0;
    double y = 0.0;
    double delta_x = 0.0;
    double delta_y = 0.0;
    int button = 0;
    std::string key;
    std::string text;
    std::string edit_text;
    bool has_edit_text = false;
    unsigned modifiers = 0;
    bool bubbles = true;
    bool propagation_stopped = false;
    bool default_prevented = false;
};

using Listener = std::function<void(Event&)>;
using TimerCallback = std::function<void()>;

std::string normalize_event_type(std::string_view type);

class EventRegistry {
public:
    using Clock = std::function<double()>;

    EventRegistry();

    std::uint64_t add_listener(const Node* node, std::string_view type, Listener listener);
    bool remove_listener(std::uint64_t id);
    void remove_listeners(const Node* node);
    std::size_t listener_count(const Node* node, std::string_view type) const;
    bool has_listeners(std::string_view type) const;

    bool dispatch(Document& document, Node* target, Event& event);
    Event* current_event() noexcept { return m_stack.empty() ? nullptr : m_stack.back(); }

    std::uint64_t set_timeout(double milliseconds, TimerCallback callback);
    std::uint64_t set_interval(double milliseconds, TimerCallback callback);
    bool clear_timer(std::uint64_t id);
    std::size_t run_due_timers();
    std::size_t timer_count() const noexcept { return m_timers.size(); }
    double next_timer_due() const;

    double now() const { return m_clock(); }
    void set_clock(Clock clock) { m_clock = std::move(clock); }

    void request_quit() noexcept { m_quit = true; }
    bool quit_requested() const noexcept { return m_quit; }

    void set_error_handler(std::function<void(const std::string&)> handler) { m_on_error = std::move(handler); }
    void report_error(const std::string& message) const;

    void clear();

private:
    struct Entry {
        std::uint64_t id = 0;
        const Node* node = nullptr;
        std::string type;
        std::shared_ptr<Listener> listener;
        bool removed = false;
    };

    struct Timer {
        std::uint64_t id = 0;
        double due = 0.0;
        double interval = 0.0;
        bool repeat = false;
        std::shared_ptr<TimerCallback> callback;
    };

    void call(const Entry& entry, Event& event);

    std::vector<std::shared_ptr<Entry>> m_entries;
    std::vector<Timer> m_timers;
    std::vector<Event*> m_stack;
    std::uint64_t m_next_id = 1;
    Clock m_clock;
    bool m_quit = false;
    std::function<void(const std::string&)> m_on_error;
};

} // namespace grove

#endif // GROVE_MARKUP_EVENTS_HPP
