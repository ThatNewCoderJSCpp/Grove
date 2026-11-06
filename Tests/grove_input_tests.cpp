#include "grove/grove.hpp"
#include "grove/api/grove_api.h"
#include "grove/runtime/runtime.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::cerr << "grove_input_tests.cpp:" << line << ": FAILED: " << what << '\n';
}

#define CHECK(expr) check((expr), #expr, __LINE__)

struct Seen {
    std::string type;
    std::string value;
    std::string edit;
    bool has_edit = false;
    std::size_t caret = 0;
    std::size_t sel_a = 0;
    std::size_t sel_b = 0;
    bool prevented = false;
};

struct Log {
    std::vector<Seen> events;
    std::string reset_on;
    std::string reset_to;
    std::string block_key;

    std::vector<Seen> of(const std::string& type) const {
        std::vector<Seen> out;
        for (const Seen& s : events) {
            if (s.type == type) out.push_back(s);
        }
        return out;
    }

    void clear() { events.clear(); }
};

void record(grove_event* event, void* user) {
    Log& log = *static_cast<Log*>(user);
    grove_event* current = grove_current_event(grove_current_document());
    Seen s;
    s.type = grove_event_type(current);
    grove_element* field = grove_event_current_target(current);
    const char* value = grove_input_string(field);
    s.value = value ? value : "";
    s.has_edit = grove_event_has_edit_text(current);
    s.edit = grove_event_edit_text(current);
    s.caret = grove_event_edit_caret(current);
    s.sel_a = grove_event_edit_selection_start(current);
    s.sel_b = grove_event_edit_selection_end(current);
    if (s.type == "key_down" && !log.block_key.empty() && log.block_key == grove_event_key(event)) {
        grove_event_prevent_default(event);
        s.prevented = true;
    }
    log.events.push_back(s);
    if (s.type == "input" && !log.reset_on.empty() && s.edit == log.reset_on) grove_set_input_string(grove_current_document(), field, log.reset_to.c_str());
}

void listen_all(grove_document* d, grove_element* field, Log& log) {
    for (const char* type : { "input", "change", "key_down", "key_up", "text" }) grove_add_event_listener(d, field, type, record, &log, nullptr);
}

Seen nth(const std::vector<Seen>& seen, std::size_t i) {
    return i < seen.size() ? seen[i] : Seen{};
}

std::vector<std::string> values_of(const std::vector<Seen>& seen) {
    std::vector<std::string> out;
    for (const Seen& s : seen) out.push_back(s.value);
    return out;
}

std::vector<std::string> edits_of(const std::vector<Seen>& seen) {
    std::vector<std::string> out;
    for (const Seen& s : seen) out.push_back(s.edit);
    return out;
}

void test_string_input_live_text() {
    grove::Project project = grove::load_project_from_source(
        "<body><input_string id=\"name\" value=\"\" /><input_integer id=\"count\" value=\"7\" /></body>", "live.grove", ".");
    grove_document* d = reinterpret_cast<grove_document*>(&project.document);
    grove_bind_document(d);
    grove_element* field = grove_find_by_id(d, "name");
    CHECK(field != nullptr);
    Log log;
    listen_all(d, field, log);

    grove::runtime::HeadlessView view(project);
    CHECK(view.ready());
    CHECK(view.focus(*project.document.find_by_id("name")));

    view.type("ab");
    const std::vector<Seen> typed = log.of("input");
    CHECK(values_of(typed) == (std::vector<std::string>{ "a", "ab" }));
    CHECK(edits_of(typed) == (std::vector<std::string>{ "a", "ab" }));
    CHECK(typed.size() == 2 && nth(typed, 0).has_edit && nth(typed, 1).caret == 2 && nth(typed, 1).sel_a == 2 && nth(typed, 1).sel_b == 2);
    const std::vector<Seen> downs = log.of("key_down");
    CHECK(downs.size() == 2 && nth(downs, 0).edit.empty() && nth(downs, 1).edit == "a" && nth(downs, 1).value == "a" && nth(downs, 1).has_edit);
    const std::vector<Seen> texts = log.of("text");
    CHECK(texts.size() == 2 && nth(texts, 1).edit == "a");
    const std::vector<Seen> ups = log.of("key_up");
    CHECK(ups.size() == 2 && nth(ups, 0).edit == "a" && nth(ups, 0).value == "a" && nth(ups, 1).edit == "ab" && nth(ups, 1).value == "ab");
    CHECK(std::string(grove_input_string(field)) == "ab");
    CHECK(view.edit_text() == "ab");

    log.clear();
    CHECK(view.key("Backspace"));
    CHECK(log.of("key_down").size() == 1 && nth(log.of("key_down"), 0).edit == "ab" && nth(log.of("key_down"), 0).value == "ab");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "a" }));
    CHECK(edits_of(log.of("input")) == (std::vector<std::string>{ "a" }));
    CHECK(nth(log.of("input"), 0).caret == 1);
    CHECK(log.of("key_up").size() == 1 && nth(log.of("key_up"), 0).edit == "a" && nth(log.of("key_up"), 0).value == "a");

    log.clear();
    view.paste("xyz");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "axyz" }));
    CHECK(edits_of(log.of("input")) == (std::vector<std::string>{ "axyz" }));
    CHECK(nth(log.of("input"), 0).caret == 4);

    log.clear();
    view.text("\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK(log.of("key_down").empty());
    CHECK(log.of("text").size() == 1 && nth(log.of("text"), 0).edit == "axyz");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "axyz\xE6\x97\xA5\xE6\x9C\xAC" }));
    CHECK(nth(log.of("input"), 0).caret == 10);

    log.clear();
    view.type("\xC3\xA9");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "axyz\xE6\x97\xA5\xE6\x9C\xAC\xC3\xA9" }));
    CHECK(nth(log.of("input"), 0).caret == 12);

    log.clear();
    CHECK(view.key("A", grove::ModifierControl));
    CHECK(log.of("input").empty());
    CHECK(log.of("key_up").size() == 1 && nth(log.of("key_up"), 0).sel_a == 0 && nth(log.of("key_up"), 0).sel_b == 12);
    view.type("Z");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "Z" }));

    log.clear();
    view.type("QR");
    CHECK(view.key("Left", grove::ModifierShift));
    CHECK(log.of("key_up").size() == 3 && nth(log.of("key_up"), 2).caret == 2 && nth(log.of("key_up"), 2).sel_a == 2 && nth(log.of("key_up"), 2).sel_b == 3);
    CHECK(view.key("Backspace"));
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "ZQ", "ZQR", "ZQ" }));

    log.clear();
    log.block_key = "x";
    view.type("x");
    CHECK(log.of("key_down").size() == 1 && nth(log.of("key_down"), 0).prevented);
    CHECK(log.of("input").empty());
    CHECK(view.edit_text() == "ZQ");
    log.block_key.clear();

    log.clear();
    log.reset_on = "ZQ!";
    log.reset_to = "fresh";
    view.type("!");
    CHECK(std::string(grove_input_string(field)) == "fresh");
    CHECK(view.edit_text() == "fresh");
    view.type("?");
    CHECK(values_of(log.of("input")) == (std::vector<std::string>{ "ZQ!", "fresh?" }));
    CHECK(log.of("change").empty());

    log.clear();
    CHECK(view.key("Enter"));
    CHECK(log.of("change").size() == 1 && nth(log.of("change"), 0).value == "fresh?" && nth(log.of("change"), 0).edit == "fresh?");

    grove_element* count = grove_find_by_id(d, "count");
    Log numbers;
    listen_all(d, count, numbers);
    CHECK(view.focus(*project.document.find_by_id("count")));
    CHECK(view.key("Backspace"));
    view.type("12");
    CHECK(edits_of(numbers.of("input")) == (std::vector<std::string>{ "", "1", "12" }));
    std::int64_t committed = 0;
    CHECK(grove_input_integer(count, &committed) && committed == 7);
    CHECK(view.key("Enter"));
    CHECK(grove_input_integer(count, &committed) && committed == 12);
    CHECK(numbers.of("change").size() == 1);

    project.document.events().clear();
    grove_bind_document(nullptr);
}

void test_c_api_without_edit() {
    grove::Project project = grove::load_project_from_source("<body><button id=\"b\">b</button></body>", "plain.grove", ".");
    grove_document* d = reinterpret_cast<grove_document*>(&project.document);
    grove_bind_document(d);
    Log log;
    grove_add_event_listener(d, grove_find_by_id(d, "b"), "click", record, &log, nullptr);
    CHECK(grove_dispatch_event(d, grove_find_by_id(d, "b"), "click"));
    CHECK(log.events.size() == 1 && !log.events[0].has_edit && log.events[0].edit.empty() && log.events[0].caret == 0);
    CHECK(!grove_event_has_edit_text(nullptr) && std::string(grove_event_edit_text(nullptr)).empty() && grove_event_edit_caret(nullptr) == 0);
    project.document.events().clear();
    grove_bind_document(nullptr);
}

} // namespace

int main() {
    test_string_input_live_text();
    test_c_api_without_edit();
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " checks failed\n";
        return 1;
    }
    std::cout << "grove_input_tests: " << g_checks << " checks passed\n";
    return 0;
}
