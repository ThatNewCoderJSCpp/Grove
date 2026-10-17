#include <grove/dom.hpp>

#include <string>

static void update_stats() {
    const grove::ElementList rows = grove::document.query_all("row.task");
    const grove::ElementList done = grove::document.query_all("row.task.done");
    grove::Element stats = grove::document.get_by_id("stats");
    if (rows.empty()) {
        stats.set_text("No tasks yet");
        return;
    }
    stats.set_text(std::to_string(done.size()) + " of " + std::to_string(rows.size()) + " done");
    grove::Element clear = grove::document.get_by_id("clear-done");
    if (done.empty()) clear.set_attribute("disabled", "");
    else clear.remove_attribute("disabled");
}

GROVE_ON_LOAD(start_stats) {
    grove::document.on("tasks_changed", [](grove::Event&) { update_stats(); });
    grove::document.on("load", [](grove::Event&) { update_stats(); });
}
