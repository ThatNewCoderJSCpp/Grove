#include "grove/grove.hpp"
#include "grove/terminal.hpp"
#include "grove/api/grove_api.h"
#include "grove/bark/properties.hpp"
#include "grove/input/code_find.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::cerr << "grove_tests.cpp:" << line << ": FAILED: " << what << '\n';
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void test_markup() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<body>\n"
        "  <window id=\"main\" class=\"a b\">\n"
        "    <p>Hi &amp; bye &#x3B1;</p>\n"
        "    <math>x < 3 && y > 2</math>\n"
        "    <br>\n"
        "    <input value=plain />\n"
        "  </window>\n"
        "</body>\n",
        "t.grove", d);
    CHECK(d.ok());
    const grove::Node* w = doc.find_by_id("main");
    CHECK(w && w->tag() == "window");
    CHECK(w && w->has_class("b"));
    const grove::Node* p = doc.first_by_tag("p");
    CHECK(p && p->inner_text() == "Hi & bye \xCE\xB1");
    const grove::Node* m = doc.first_by_tag("math");
    CHECK(m && m->inner_text() == "x < 3 && y > 2");
    const grove::Node* in = doc.first_by_tag("input");
    CHECK(in && in->attribute_or("value", "") == "plain");
    CHECK(in && in->parent() == w);
}

void test_markup_errors() {
    grove::Diagnostics d;
    grove::parse_markup("<body><window><p>open</window></body><math>never closed", "t.grove", d);
    CHECK(d.error_count() == 2);
    grove::Diagnostics d2;
    grove::parse_markup("<widget>hi</widget>", "t.grove", d2);
    CHECK(d2.ok() && d2.warning_count() == 1);
    grove::Diagnostics d3;
    grove::parse_markup("<my-widget>hi</my-widget>", "t.grove", d3);
    CHECK(d3.empty());
}

void test_bark() {
    grove::Diagnostics d;
    grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(
        "$c = #ff0000;\n"
        "window { color: $c; size: 640px 480px; }\n"
        "panel.card { background: rgb(1, 2, 3); > p { color: blue; } }\n"
        "#x { color: hsl(120, 100%, 50%) !important; }\n",
        "t.bark", d);
    CHECK(d.ok());
    CHECK(sheet.rules().size() == 4);
    CHECK(sheet.rules()[2].selectors[0].to_string() == "panel.card > p");

    grove::Document doc = grove::parse_markup("<window><panel class=\"card\"><p id=\"x\" style=\"color: white\">t</p></panel></window>", "t.grove", d);
    grove::bark::StyleEngine engine;
    engine.add(sheet);
    const grove::Node* p = doc.find_by_id("x");
    const grove::bark::ComputedStyle& s = engine.style_of(*p, d);
    const auto color = s.color("color");
    CHECK(color && color->r == 0 && color->g == 255 && color->b == 0);
    const grove::Node* w = doc.first_by_tag("window");
    const auto& ws = engine.style_of(*w, d);
    CHECK(ws.length("size", 1) && *ws.length("size", 1) == 480.0);
    CHECK(ws.color("color") && ws.color("color")->r == 255);
}

void test_change_stamps_and_style_cache() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup("<window><panel id=\"outer\" class=\"a\"><p id=\"inner\">t</p><p id=\"second\">u</p></panel></window>", "t.grove", d);
    grove::Node* outer = doc.find_by_id("outer");
    grove::Node* inner = doc.find_by_id("inner");
    grove::Node* second = doc.find_by_id("second");
    grove::Node* window = doc.first_by_tag("window");
    const std::uint64_t window_before = window->tree_stamp();
    const std::uint64_t outer_self = outer->self_stamp();
    const std::uint64_t second_before = second->tree_stamp();
    inner->set_attribute("class", "hot");
    CHECK(inner->tree_stamp() > window_before);
    CHECK(window->tree_stamp() == inner->tree_stamp());
    CHECK(outer->self_stamp() == outer_self);
    CHECK(second->tree_stamp() == second_before);
    const std::uint64_t same = inner->tree_stamp();
    inner->set_attribute("class", "hot");
    CHECK(inner->tree_stamp() == same);
    inner->set_state(grove::StateHover, false);
    CHECK(inner->tree_stamp() == same);
    inner->set_state(grove::StateHover, true);
    CHECK(inner->tree_stamp() > same);
    const std::uint64_t before_child = outer->self_stamp();
    doc.append_child(*outer, *doc.create_element("p"));
    CHECK(outer->self_stamp() == before_child);
    CHECK(outer->tree_stamp() > before_child);
    const grove::Node* text_node = second->children().front().get();
    const std::uint64_t text_same = second->tree_stamp();
    second->set_inner_text("u");
    CHECK(second->tree_stamp() == text_same);
    second->set_inner_text("v");
    CHECK(second->children().size() == 1 && second->children().front().get() == text_node);
    CHECK(second->inner_text() == "v");
    CHECK(second->tree_stamp() > text_same);
    for (int i = 0; i < 200; ++i) doc.remove(*doc.append_child(*outer, *doc.create_element("p")));
    grove::Node* fresh = doc.create_element("p");
    CHECK(doc.append_child(*outer, *fresh) == fresh);
    CHECK(doc.remove(*fresh));
    CHECK(doc.append_child(*second, *fresh) == fresh);

    grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(
        "p { color: #010101; }\n"
        ".a p { color: #020202; }\n"
        ".b p { color: #030303; }\n"
        "p.hot { font-size: 20px; }\n"
        "p:hover { background: #0a0a0a; }\n"
        "p:first-child { margin: 3px; }\n",
        "t.bark", d);
    grove::bark::StyleEngine engine;
    engine.add(sheet);
    CHECK(engine.style_of(*second, d).color("color")->r == 2);
    CHECK(engine.style_of(*inner, d).length("font-size") && *engine.style_of(*inner, d).length("font-size") == 20.0);
    CHECK(engine.style_of(*inner, d).color("background")->r == 10);
    CHECK(engine.style_of(*inner, d).get("margin") != nullptr);
    CHECK(engine.style_of(*second, d).get("margin") == nullptr);
    const std::uint64_t key = engine.style_key(*second, d);
    engine.invalidate();
    CHECK(engine.style_key(*second, d) == key);
    outer->set_attribute("class", "b");
    engine.invalidate();
    CHECK(engine.style_of(*second, d).color("color")->r == 3);
    CHECK(engine.style_key(*second, d) != key);
    inner->set_state(grove::StateHover, false);
    engine.invalidate();
    CHECK(engine.style_of(*inner, d).get("background") == nullptr);
    std::unique_ptr<grove::Node> taken = outer->remove_child(inner);
    engine.invalidate();
    CHECK(engine.style_of(*second, d).get("margin") != nullptr);
    outer->insert_child(0, std::move(taken));
    engine.invalidate();
    CHECK(engine.style_of(*second, d).get("margin") == nullptr);
    grove::bark::StyleEngine copy = engine;
    CHECK(copy.style_of(*second, d).color("color")->r == 3);

    grove::bark::ComputedStyle cs;
    cs.set("display", grove::bark::ValueList{ grove::bark::Value::make_ident("row") });
    cs.set("margin", grove::bark::ValueList{ grove::bark::Value::make_number(1, "px"), grove::bark::Value::make_number(2, "px") });
    CHECK(cs.text_view("display") == "row");
    CHECK(cs.text_view(std::string("display")) == "row");
    CHECK(cs.text_view("missing").empty());
    CHECK(cs.get("margin") && cs.get("margin")->size() == 2);
    const grove::bark::ComputedStyle moved_from = cs;
    grove::bark::ComputedStyle assigned;
    assigned = moved_from;
    CHECK(assigned.text_view("display") == "row");
    assigned.set("display", grove::bark::ValueList{ grove::bark::Value::make_ident("none") });
    CHECK(assigned.text_view("display") == "none");
    CHECK(moved_from.text_view("display") == "row");
    CHECK(*assigned.text("margin") == *moved_from.text("margin"));
}

void test_bark_errors() {
    grove::Diagnostics d;
    grove::bark::parse_stylesheet("a { color: $nope; }\nb { color #fff; }\nc { color: #zz; }", "t.bark", d);
    CHECK(d.error_count() == 3);
}

void test_math() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup("<math>(x+2)/(3*exp(4))</math><math syntax=\"brace\">frac{1, 2}</math><math syntax=\"brace\">oops</math>", "t.grove", d);
    std::vector<grove::MathBlock> blocks = grove::collect_math(doc, d);
    CHECK(blocks.size() == 3);
    CHECK(d.error_count() == 1);
    CHECK(grove::to_brace_form(blocks[1]) == "frac{1, 2}");
}

void test_scripts() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<script>let x = 1;</script>"
        "<script lang=\"c\">int y;</script>"
        "<script lang=\"rust\">fn main() {}</script>",
        "t.grove", d);
    std::vector<grove::ScriptBlock> scripts = grove::collect_scripts(doc, ".", d);
    CHECK(scripts.size() == 2);
    CHECK(scripts[0].language == grove::ScriptLanguage::Walnut);
    CHECK(scripts[1].language == grove::ScriptLanguage::C);
    CHECK(d.error_count() == 1);
}

void test_api() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup("<window><h1 id=\"t\">old</h1></window>", "t.grove", d);
    grove_bind_document(reinterpret_cast<grove_document*>(&doc));
    const auto before = doc.revision();
    CHECK(grove_set_text_by_id("t", "new") == 1);
    CHECK(doc.find_by_id("t")->inner_text() == "new");
    CHECK(grove_set_attribute_by_id("t", "class", "big") == 1);
    CHECK(std::string(grove_get_attribute_by_id("t", "class")) == "big");
    CHECK(doc.revision() == before + 2);
    CHECK(grove_set_text_by_id("missing", "x") == 0);
    grove_bind_document(nullptr);
}

void test_body_is_window() {
    grove::Project project = grove::load_project_from_source(
        "<body title=\"t\"><p>hi</p><window id=\"extra\"><p>later</p></window></body>", "t.grove", ".");
    CHECK(project.diagnostics.empty());
    const grove::Node* extra = project.document.find_by_id("extra");
    CHECK(extra && project.styles.style_of(*extra, project.diagnostics).text("display").value_or("") == "none");
    grove::Project bare = grove::load_project_from_source("<p>hi</p>", "t.grove", ".");
    CHECK(bare.diagnostics.ok() && bare.diagnostics.items().size() == 1);
}

void test_viewport_units() {
    grove::Diagnostics d;
    grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(
        "body { padding: 10vwidth 10vheight; font-size: 2vmin; width: 1vdiag; }\n"
        "p { font-size: 1.5em; margin: 5vmax; }\n",
        "t.bark", d);
    CHECK(d.ok());
    grove::Document doc = grove::parse_markup("<body><p id=\"p\">x</p></body>", "t.grove", d);
    grove::bark::StyleEngine engine;
    engine.add(sheet);
    engine.set_viewport(grove::bark::Viewport{ 800, 600 });
    const auto& body = engine.style_of(*doc.first_by_tag("body"), d);
    CHECK(body.length("padding", 0) && *body.length("padding", 0) == 80.0);
    CHECK(body.length("padding", 1) && *body.length("padding", 1) == 60.0);
    CHECK(body.font_size() == 12.0);
    CHECK(body.length("width") && *body.length("width") == 10.0);
    const auto& p = engine.style_of(*doc.find_by_id("p"), d);
    CHECK(p.font_size() == 18.0);
    CHECK(p.length("margin") && *p.length("margin") == 40.0);
    engine.set_viewport(grove::bark::Viewport{ 400, 1000 });
    CHECK(engine.style_of(*doc.first_by_tag("body"), d).font_size() == 8.0);
    CHECK(grove::bark::as_length(grove::bark::ValueList{ grove::bark::Value::make_number(10, "vw") }, 16.0) == std::nullopt);
}

void test_inputs() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<body>"
        "<input_integer id=\"i\" value=\"5\" min=\"0\" max=\"10\" />"
        "<input_float id=\"f\" value=\"-1.5\" />"
        "<input_string id=\"s\" maxlength=\"3\" />"
        "<input_bool id=\"b\" checked>On</input_bool>"
        "<input_radio id=\"r1\" name=\"g\" value=\"a\" checked>A</input_radio>"
        "<input_radio id=\"r2\" name=\"g\" value=\"b\">B</input_radio>"
        "<input_slider id=\"sl\" min=\"0\" max=\"1\" step=\"0.25\" />"
        "<input_color id=\"c\" value=\"#ff8000\" />"
        "<input type=\"integer\" id=\"gi\" value=\"7\" />"
        "</body>",
        "t.grove", d);
    grove::check_inputs(doc, d);
    CHECK(d.ok() && d.empty());
    grove::Node& i = *doc.find_by_id("i");
    grove::Node& f = *doc.find_by_id("f");
    grove::Node& s = *doc.find_by_id("s");
    grove::Node& sl = *doc.find_by_id("sl");

    CHECK(grove::input_value<std::int64_t>(i) == std::int64_t(5));
    CHECK(!grove::input_value<double>(i));
    CHECK(!grove::input_value<std::string>(i));
    CHECK(grove::input_value<double>(f) == -1.5);
    CHECK(grove::input_value<bool>(*doc.find_by_id("b")) == true);
    CHECK(grove::input_integer(*doc.find_by_id("gi")) == std::int64_t(7));
    CHECK(grove::input_color(*doc.find_by_id("c"))->g == 0x80);

    CHECK(grove::input_accepts(i, "", "7"));
    CHECK(!grove::input_accepts(i, "", "a"));
    CHECK(!grove::input_accepts(i, "", "-"));
    CHECK(!grove::input_accepts(i, "1", "."));
    CHECK(grove::input_accepts(f, "", "-"));
    CHECK(grove::input_accepts(f, "1", "."));
    CHECK(!grove::input_accepts(f, "1.2", "."));
    CHECK(!grove::input_accepts(s, "abc", "d"));
    CHECK(!grove::input_accepts(*doc.find_by_id("c"), "#", "g"));

    CHECK(grove::commit_input_text(doc, i, "42"));
    CHECK(grove::input_integer(i) == std::int64_t(10));
    CHECK(!grove::commit_input_text(doc, i, ""));
    CHECK(!grove::commit_input_text(doc, i, "99999999999999999999"));
    CHECK(!grove::commit_input_text(doc, f, "1.2.3"));
    CHECK(grove::set_input_value(doc, s, std::string("abcdef")) && grove::input_string(s) == std::string("abc"));
    CHECK(!grove::set_input_integer(doc, f, 3));
    CHECK(grove::set_input_float(doc, sl, 0.6) && grove::input_float(sl) == 0.5);
    CHECK(grove::step_input(doc, i, -3) && grove::input_integer(i) == std::int64_t(7));

    CHECK(grove::radio_choice(doc, "g") == std::string("a"));
    CHECK(grove::set_radio_choice(doc, "g", "b"));
    CHECK(grove::radio_choice(doc, "g") == std::string("b"));
    CHECK(!doc.find_by_id("r1")->has_attribute("checked"));

    CHECK(grove::parse_integer("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    CHECK(!grove::parse_integer("9223372036854775808"));
    CHECK(!grove::parse_integer("1e3"));
}

void test_input_errors() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<input />"
        "<input type=\"date\" />"
        "<input_integer value=\"1.5\" min=\"5\" max=\"1\" />"
        "<input_string min=\"3\" />"
        "<input_radio value=\"x\">no name</input_radio>"
        "<input_radio name=\"g\" value=\"a\" checked>a</input_radio>"
        "<input_radio name=\"g\" value=\"b\" checked>b</input_radio>"
        "<input_color value=\"red\" />",
        "t.grove", d);
    grove::check_inputs(doc, d);
    CHECK(d.error_count() == 7);
    CHECK(d.warning_count() == 1);
}

void test_input_api() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<input_integer id=\"n\" value=\"4\" max=\"10\" /><input_string id=\"s\" value=\"hi\" />"
        "<input_radio name=\"g\" value=\"x\">x</input_radio><input_radio name=\"g\" value=\"y\" checked>y</input_radio>",
        "t.grove", d);
    grove_bind_document(reinterpret_cast<grove_document*>(&doc));
    CHECK(grove_input_kind_by_id("n") == GROVE_INPUT_INTEGER);
    CHECK(grove_get_integer_by_id("n") == 4);
    CHECK(grove_set_integer_by_id("n", 50) && grove_get_integer_by_id("n") == 10);
    CHECK(!grove_set_integer_by_id("s", 1));
    CHECK(!grove_set_string_by_id("n", "x"));
    CHECK(std::string(grove_get_string_by_id("s")) == "hi");
    CHECK(std::string(grove_get_string_by_id("n")).empty());
    CHECK(std::string(grove_get_choice("g")) == "y");
    CHECK(grove_set_choice("g", "x") && std::string(grove_get_choice("g")) == "x");
    int64_t out = 0;
    CHECK(grove_input_integer(grove_find_by_id(reinterpret_cast<grove_document*>(&doc), "n"), &out) && out == 10);
    double fout = 0;
    CHECK(!grove_input_float(grove_find_by_id(reinterpret_cast<grove_document*>(&doc), "n"), &fout));
    grove_bind_document(nullptr);
}

void test_switch() {
    grove::Project project = grove::load_project_from_source(
        "<style>.t { appearance: switch; }</style>"
        "<body><input_bool id=\"a\">a</input_bool><input_bool id=\"b\" switch>b</input_bool>"
        "<input_bool id=\"c\" class=\"t\">c</input_bool><input type=\"toggle\" id=\"d\" />"
        "<input_integer id=\"e\" switch /></body>",
        "t.grove", ".");
    CHECK(project.diagnostics.error_count() == 0 && project.diagnostics.warning_count() == 1);
    auto appearance = [&](const char* id) { return project.styles.style_of(*project.document.find_by_id(id), project.diagnostics).text("appearance"); };
    CHECK(!grove::is_switch(*project.document.find_by_id("a"), appearance("a")));
    CHECK(grove::is_switch(*project.document.find_by_id("b"), appearance("b")));
    CHECK(grove::is_switch(*project.document.find_by_id("c"), appearance("c")));
    CHECK(grove::is_switch(*project.document.find_by_id("d"), appearance("d")));
    CHECK(!grove::is_switch(*project.document.find_by_id("e"), appearance("e")));
}

void test_highlight() {
    const std::string bark = grove::highlight_json("$a = #ff0000;\n#main { colr: $a; color: $nope; background: rgba(0, 0, 255, 50%); }\nbogus { width: 1px; }\n", "t.bark");
    CHECK(bark.find("[0,0,2,\"variable\",[\"declaration\"]]") != std::string::npos);
    CHECK(bark.find("[1,8,4,\"unresolved\",[]]") != std::string::npos);
    CHECK(bark.find("[1,14,2,\"variable\",[]]") != std::string::npos);
    CHECK(bark.find("[1,25,5,\"unresolved\",[]]") != std::string::npos);
    CHECK(bark.find("[2,0,5,\"unresolved\",[]]") != std::string::npos);
    CHECK(bark.find("{\"line\":0,\"col\":5,\"endLine\":0,\"endCol\":12,\"r\":255,\"g\":0,\"b\":0,\"a\":255}") != std::string::npos);
    CHECK(bark.find("\"col\":44,\"endLine\":1,\"endCol\":64,\"r\":0,\"g\":0,\"b\":255,\"a\":128") != std::string::npos);
    CHECK(bark.find("unknown property 'colr'") != std::string::npos);
    CHECK(bark.find("unknown variable $nope") != std::string::npos);
    CHECK(bark.find("there is no <bogus> element") != std::string::npos);

    const std::string grove_text = grove::highlight_json("<body>\n<input_color value=\"#00ff80\" />\n<my-card style=\"color: red\">x</my-card>\n</body>\n", "t.grove");
    CHECK(grove_text.find("[1,1,11,\"tag\",[\"input\"]]") != std::string::npos);
    CHECK(grove_text.find("[2,1,7,\"tag\",[\"custom\"]]") != std::string::npos);
    CHECK(grove_text.find("[2,16,5,\"property\",[]]") != std::string::npos);
    CHECK(grove_text.find("{\"line\":1,\"col\":20,\"endLine\":1,\"endCol\":27,\"r\":0,\"g\":255,\"b\":128,\"a\":255}") != std::string::npos);
    CHECK(grove_text.find("{\"line\":2,\"col\":23,\"endLine\":2,\"endCol\":26,\"r\":255") != std::string::npos);
    CHECK(grove_text.find("\"diagnostics\":[]") != std::string::npos);
}

void test_example() {
    grove::Project project = grove::load_project(std::string(GROVE_EXAMPLES_DIR) + "/hello/hello.grove");
    if (!project.diagnostics.ok()) project.diagnostics.print(std::cerr);
    CHECK(project.diagnostics.ok());
    CHECK(project.math.size() == 2);
    CHECK(project.scripts.size() == 2);
}

void test_inputs_example() {
    grove::Project project = grove::load_project(std::string(GROVE_EXAMPLES_DIR) + "/inputs/inputs.grove");
    if (!project.diagnostics.ok()) project.diagnostics.print(std::cerr);
    CHECK(project.diagnostics.ok() && project.diagnostics.warning_count() == 0);
}

grove::Document parse_doc(const char* source) {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(source, "t.grove", d);
    CHECK(d.ok());
    return doc;
}

void test_events() {
    grove::Document doc = parse_doc("<body><div id=\"outer\"><button id=\"b\">Go</button></div></body>");
    grove::Node* outer = doc.find_by_id("outer");
    grove::Node* b = doc.find_by_id("b");
    std::vector<std::string> order;
    doc.events().add_listener(b, "click", [&](grove::Event& e) { order.push_back("button:" + e.target->id()); });
    doc.events().add_listener(outer, "Click", [&](grove::Event& e) { order.push_back("outer:" + e.current_target->id()); });
    doc.events().add_listener(nullptr, "click", [&](grove::Event&) { order.push_back("document"); });
    grove::Event click;
    click.type = "click";
    doc.events().dispatch(doc, b, click);
    CHECK(order.size() == 3 && order[0] == "button:b" && order[1] == "outer:outer" && order[2] == "document");

    order.clear();
    const std::uint64_t stopper = doc.events().add_listener(outer, "click", [&](grove::Event& e) { e.propagation_stopped = true; });
    grove::Event again;
    again.type = "click";
    doc.events().dispatch(doc, b, again);
    CHECK(order.size() == 2 && order.back() == "outer:outer");
    CHECK(doc.events().remove_listener(stopper));
    CHECK(!doc.events().remove_listener(stopper));

    order.clear();
    grove::Event focus;
    focus.type = "focus";
    focus.bubbles = false;
    doc.events().add_listener(outer, "focus", [&](grove::Event&) { order.push_back("outer-focus"); });
    doc.events().dispatch(doc, b, focus);
    CHECK(order.empty());

    doc.events().add_listener(b, "key_down", [](grove::Event& e) { e.default_prevented = true; });
    grove::Event key;
    key.type = "keydown";
    CHECK(!doc.events().dispatch(doc, b, key));
    CHECK(key.default_prevented);
    CHECK(grove::normalize_event_type("Mouse-Down") == "mousedown");
    CHECK(grove::normalize_event_type("double_click") == "doubleclick");
    CHECK(doc.events().listener_count(b, "click") == 1);
    doc.events().remove_listeners(b);
    CHECK(doc.events().listener_count(b, "click") == 0);
}

void test_timers() {
    grove::Document doc = parse_doc("<body></body>");
    double now = 0.0;
    doc.events().set_clock([&now] { return now; });
    int once = 0;
    int ticks = 0;
    doc.events().set_timeout(100, [&] { ++once; });
    const std::uint64_t interval = doc.events().set_interval(30, [&] { ++ticks; });
    now = 50;
    doc.events().run_due_timers();
    CHECK(once == 0 && ticks == 1);
    now = 100;
    doc.events().run_due_timers();
    CHECK(once == 1 && ticks == 2);
    now = 200;
    doc.events().run_due_timers();
    CHECK(once == 1 && ticks >= 3);
    CHECK(doc.events().clear_timer(interval));
    const int before = ticks;
    now = 400;
    doc.events().run_due_timers();
    CHECK(ticks == before);
    CHECK(doc.events().timer_count() == 0);
    doc.events().request_quit();
    CHECK(doc.events().quit_requested());
}

void test_query() {
    grove::Document doc = parse_doc(
        "<body><ul id=\"list\"><li class=\"item done\">a</li><li class=\"item\">b</li><li class=\"item\" id=\"last\">c</li></ul>"
        "<p></p><input_bool id=\"flag\" checked>On</input_bool></body>");
    grove::Node& root = doc.root();
    CHECK(grove::query(root, "#last") == doc.find_by_id("last"));
    CHECK(grove::query_all(root, ".item").size() == 3);
    CHECK(grove::query_all(root, "ul > li.done").size() == 1);
    CHECK(grove::query(root, "li:first-child") && grove::query(root, "li:first-child")->inner_text() == "a");
    CHECK(grove::query(root, "li:last-child") == doc.find_by_id("last"));
    CHECK(grove::query(root, "p:empty") != nullptr);
    CHECK(grove::query(root, "input_bool:checked") == doc.find_by_id("flag"));
    CHECK(grove::query_all(root, "li, p").size() == 4);
    grove::Diagnostics d;
    CHECK(grove::query(root, "li[", &d) == nullptr);
    CHECK(!d.ok());
    grove::Node* list = doc.find_by_id("list");
    CHECK(list && grove::query_all(*list, "li").size() == 3);

    grove::Node* b = grove::query_all(root, ".item")[1];
    b->set_state(grove::StateHover, true);
    CHECK(grove::query(root, "li:hover") == b);
    b->set_state(grove::StateHover, false);
    CHECK(grove::query(root, "li:hover") == nullptr);
}

void test_pseudo_styles() {
    grove::Project project = grove::load_project_from_source(
        "<style>button { color: #111111; } button:hover { color: #222222; } button:disabled { color: #333333; } li:first-child { color: #444444; }</style>"
        "<body><button id=\"b\">x</button><button id=\"d\" disabled>y</button><ul><li id=\"one\">1</li><li id=\"two\">2</li></ul></body>",
        "t.grove", "");
    CHECK(project.diagnostics.ok());
    grove::Node* b = project.document.find_by_id("b");
    auto color = [&](const grove::Node& n) { return project.styles.style_of(n, project.diagnostics).color("color"); };
    CHECK(color(*b) && color(*b)->r == 0x11);
    b->set_state(grove::StateHover, true);
    project.styles.invalidate();
    CHECK(color(*b) && color(*b)->r == 0x22);
    CHECK(color(*project.document.find_by_id("d"))->r == 0x33);
    CHECK(color(*project.document.find_by_id("one"))->r == 0x44);
    CHECK(!color(*project.document.find_by_id("two")) || color(*project.document.find_by_id("two"))->r != 0x44);
    grove::Diagnostics d;
    grove::bark::parse_stylesheet("p:bogus { color: red; }", "t.bark", d);
    CHECK(d.warning_count() == 1);
}

void test_tree_editing() {
    grove::Document doc = parse_doc("<body><ul id=\"list\"><li id=\"a\">a</li></ul></body>");
    grove::Node* list = doc.find_by_id("list");
    const auto rev = doc.revision();
    grove::Node* li = doc.create_element("li");
    li->set_attribute("id", "b");
    doc.append_child(*li, *doc.create_text("b"));
    CHECK(!doc.is_attached(*li));
    doc.append_child(*list, *li);
    CHECK(doc.is_attached(*li));
    CHECK(doc.revision() != rev);
    CHECK(doc.find_by_id("b") == li);
    grove::Node* first = doc.create_element("li");
    doc.insert_before(*list, *first, doc.find_by_id("a"));
    CHECK(list->children().front().get() == first);
    CHECK(doc.remove(*li));
    CHECK(!doc.is_attached(*li));
    CHECK(li->inner_text() == "b");
    CHECK(doc.find_by_id("b") == nullptr);
    doc.append_child(*list, *li);
    CHECK(doc.find_by_id("b") == li);

    grove::Node* a = doc.find_by_id("a");
    CHECK(grove::add_class(doc, *a, "x"));
    CHECK(!grove::add_class(doc, *a, "x"));
    CHECK(a->has_class("x"));
    CHECK(grove::toggle_class(doc, *a, "y") && a->has_class("y"));
    CHECK(!grove::toggle_class(doc, *a, "y") && !a->has_class("y"));
    CHECK(grove::remove_class(doc, *a, "x") && !a->has_class("x"));
    grove::set_inline_style(doc, *a, "color", "red");
    grove::set_inline_style(doc, *a, "padding", "4px");
    CHECK(grove::inline_style(*a, "color").value_or("") == "red");
    grove::set_inline_style(doc, *a, "color", "");
    CHECK(!grove::inline_style(*a, "color"));
    CHECK(grove::inline_style(*a, "padding").value_or("") == "4px");
}

int g_c_clicks = 0;
int g_c_released = 0;

void c_listener(grove_event* event, void* user_data) {
    g_c_clicks += *static_cast<int*>(user_data);
    if (std::string(grove_event_type(event)) == "click") grove_event_stop_propagation(event);
}

void c_release(void*) { ++g_c_released; }

void test_c_events() {
    grove::Document doc = parse_doc("<body><div id=\"box\"><button id=\"go\" class=\"btn\">Go</button></div></body>");
    grove_document* d = reinterpret_cast<grove_document*>(&doc);
    grove_bind_document(d);
    grove_element* go = grove_query(d, nullptr, "#box .btn");
    CHECK(go != nullptr && std::string(grove_id(go)) == "go");
    grove_element* found[4] = {};
    CHECK(grove_query_all(d, nullptr, "div, button", found, 4) == 2);
    int step = 5;
    const uint64_t id = grove_add_event_listener(d, go, "click", c_listener, &step, c_release);
    CHECK(id != 0);
    CHECK(grove_dispatch_event(d, go, "click"));
    CHECK(g_c_clicks == 5);
    CHECK(grove_remove_event_listener(d, id));
    CHECK(g_c_released == 1);
    CHECK(grove_dispatch_event(d, go, "click"));
    CHECK(g_c_clicks == 5);
    grove_element* made = grove_create_element(d, "span");
    grove_set_text(d, made, "new");
    CHECK(grove_append_child(d, grove_body(d), made));
    CHECK(grove_is_attached(d, made));
    CHECK(std::string(grove_text(made)) == "new");
    grove_add_class(d, made, "hot");
    CHECK(grove_has_class(made, "hot"));
    grove_set_style(d, made, "color", "blue");
    CHECK(std::string(grove_get_style(made, "color")) == "blue");
    CHECK(grove_remove_element(d, made));
    CHECK(!grove_is_attached(d, made));
    doc.events().clear();
    grove_bind_document(nullptr);
}

void test_bundle() {
    grove::Bundle bundle;
    bundle.set_meta("document", "app.grove");
    bundle.add("file", "app.grove", "<body>hi</body>");
    bundle.add("module", "0", std::string("\0\1\2binary", 9));
    const std::optional<grove::Bundle> back = grove::parse_bundle(grove::serialize_bundle(bundle));
    CHECK(back.has_value());
    CHECK(back && back->meta("document") == "app.grove");
    CHECK(back && back->find("module", "0") && back->find("module", "0")->data.size() == 9);
    CHECK(back && back->hash() == bundle.hash());
    CHECK(!grove::parse_bundle("nonsense"));

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "grove-bundle-test";
    std::filesystem::create_directories(dir);
    const std::string stub = (dir / "stub").string();
    {
        std::ofstream f(stub, std::ios::binary);
        f << "pretend this is a program";
    }
    std::string error;
    CHECK(grove::write_bundled_executable(stub, (dir / "out").string(), bundle, error));
    const std::optional<grove::Bundle> read = grove::read_embedded_bundle((dir / "out").string());
    CHECK(read && read->find("file", "app.grove") && read->find("file", "app.grove")->data == "<body>hi</body>");
    CHECK(grove::embedded_bundle_offset((dir / "out").string()).value_or(0) == 25);
    CHECK(grove::write_bundled_executable((dir / "out").string(), (dir / "again").string(), bundle, error));
    CHECK(grove::embedded_bundle_offset((dir / "again").string()).value_or(0) == 25);
    CHECK(!grove::read_embedded_bundle(stub));
    std::filesystem::remove_all(dir);

    grove::set_virtual_files({ { "/virtual/app.grove", "<style src=\"look.bark\"></style><body><p id=\"p\">x</p></body>" }, { "/virtual/look.bark", "p { color: #123456; }" } });
    grove::Project project = grove::load_project("/virtual/app.grove");
    CHECK(project.diagnostics.ok());
    const grove::Node* p = project.document.find_by_id("p");
    CHECK(p && project.styles.style_of(*p, project.diagnostics).color("color").value_or(grove::bark::Rgba{}).g == 0x34);
    CHECK(grove::path_exists("/virtual/look.bark"));
    grove::set_virtual_files({});
    CHECK(!grove::path_exists("/virtual/look.bark"));
}

void test_todo_example() {
    grove::Project project = grove::load_project(std::string(GROVE_EXAMPLES_DIR) + "/todo/todo.grove");
    if (!project.diagnostics.ok()) project.diagnostics.print(std::cerr);
    CHECK(project.diagnostics.ok());
    CHECK(project.scripts.size() == 2);
}

void test_windows() {
    grove::Document doc = parse_doc(
        "<body><button id=\"b\">Go</button>"
        "<window id=\"settings\" title=\"Settings\" width=\"300\" height=\"200\"><input_bool id=\"dark\">Dark</input_bool>"
        "<window id=\"inner\"><p id=\"deep\">x</p></window></window></body>");
    grove::Node* body = doc.first_by_tag("body");
    grove::Node* settings = doc.find_by_id("settings");
    grove::Node* inner = doc.find_by_id("inner");
    CHECK(grove::main_window(doc) == body);
    CHECK(grove::window_of(doc, doc.find_by_id("b")) == body);
    CHECK(grove::window_of(doc, doc.find_by_id("dark")) == settings);
    CHECK(grove::window_of(doc, doc.find_by_id("deep")) == inner);
    CHECK(grove::owner_window(doc, *inner) == settings);
    CHECK(grove::owner_window(doc, *settings) == body);
    CHECK(grove::owner_window(doc, *body) == nullptr);
    CHECK(grove::window_is_open(doc, *body));
    CHECK(!grove::window_is_open(doc, *settings));
    const auto rev = doc.revision();
    CHECK(grove::open_window(doc, *settings));
    CHECK(grove::window_is_open(doc, *settings));
    CHECK(doc.revision() != rev);
    double w = 0;
    double h = 0;
    CHECK(grove::window_size(doc, *settings, w, h) && w == 300 && h == 200);
    CHECK(grove::close_window(doc, *settings));
    CHECK(!grove::window_is_open(doc, *settings));
    CHECK(!grove::open_window(doc, *doc.find_by_id("b")));
    CHECK(!doc.events().quit_requested());
    grove::close_window(doc, *body);
    CHECK(doc.events().quit_requested());

    std::vector<std::string> heard;
    doc.events().add_listener(body, "click", [&](grove::Event& e) { heard.push_back("body:" + e.target->id()); });
    doc.events().add_listener(inner, "click", [&](grove::Event&) { heard.push_back("inner"); });
    grove::Event click;
    click.type = "click";
    doc.events().dispatch(doc, doc.find_by_id("dark"), click);
    CHECK(heard.size() == 1 && heard[0] == "body:dark");
}

void test_popups() {
    grove::Document doc = parse_doc("<body><window id=\"tool\" open><button id=\"t\">T</button></window></body>");
    grove::Node* body = doc.first_by_tag("body");
    grove::Node* tool = doc.find_by_id("tool");

    grove::PopupOptions info;
    info.kind = "warning";
    info.message = "Careful\nReally";
    grove::Node* warn = grove::create_popup(doc, nullptr, info);
    CHECK(warn && grove::is_popup(*warn));
    CHECK(warn->parent() == body);
    CHECK(warn->attribute_or("title", "") == "Warning");
    CHECK(grove::query_all(*warn, ".popup-message").size() == 2);
    CHECK(grove::query(*warn, ".popup-cancel") == nullptr);
    CHECK(grove::open_popups(doc, *body).size() == 1);

    grove::PopupOptions ask;
    ask.kind = "question";
    ask.title = "Age";
    ask.input = "integer";
    ask.value = "7";
    ask.options = { "0", "120" };
    grove::Node* age = grove::create_popup(doc, doc.find_by_id("t"), ask);
    CHECK(age && age->parent() == tool);
    grove::Node* field = grove::query(*age, ".popup-input");
    CHECK(field && field->tag() == "input_integer" && field->attribute_or("max", "") == "120");
    CHECK(grove::open_popups(doc, *tool).size() == 1);
    std::string answered;
    int bubbled = 0;
    doc.events().add_listener(age, "ok", [&](grove::Event&) { answered = grove::input_text(*grove::query(*age, ".popup-input")); });
    doc.events().add_listener(tool, "ok", [&](grove::Event&) { ++bubbled; });
    grove::set_input_integer(doc, *field, 42);
    grove::Event click;
    click.type = "click";
    doc.events().dispatch(doc, grove::query(*age, ".popup-ok"), click);
    CHECK(answered == "42");
    CHECK(bubbled == 1);
    CHECK(age->attribute_or("value", "") == "42");
    CHECK(!doc.is_attached(*age));
    CHECK(!grove::answer_popup(doc, *age, true));
    CHECK(grove::open_popups(doc, *tool).empty());

    grove::PopupOptions pick;
    pick.input = "choice";
    pick.options = grove::split_popup_options("Small|Medium|Large");
    pick.value = "Large";
    grove::Node* choice = grove::create_popup(doc, nullptr, pick);
    CHECK(grove::query_all(*choice, "input_radio").size() == 3);
    bool cancelled = false;
    doc.events().add_listener(choice, "cancel", [&](grove::Event&) { cancelled = true; });
    CHECK(grove::answer_popup(doc, *choice, false));
    CHECK(cancelled && choice->attribute_or("value", "") == "Large");
    doc.events().run_due_timers();

    grove_document* d = reinterpret_cast<grove_document*>(&doc);
    grove_bind_document(d);
    grove_element* p = grove_popup(d, nullptr, "error", "Oops", "Broken", "string", "abc", "");
    CHECK(p != nullptr);
    CHECK(std::string(grove_input_text(grove_query(d, p, ".popup-input"))) == "abc");
    CHECK(grove_window_is_open(d, p));
    CHECK(grove_popup_answer(d, p, true));
    CHECK(!grove_window_is_open(d, p));
    CHECK(grove_main_window(d) == reinterpret_cast<grove_element*>(body));
    CHECK(grove_window_of(d, grove_find_by_id(d, "t")) == reinterpret_cast<grove_element*>(tool));
    CHECK(std::string(grove_input_text(reinterpret_cast<grove_element*>(body))).empty());
    doc.events().clear();
    grove_bind_document(nullptr);
}

void test_windows_example() {
    grove::Project project = grove::load_project(std::string(GROVE_EXAMPLES_DIR) + "/windows/windows.grove");
    if (!project.diagnostics.ok()) project.diagnostics.print(std::cerr);
    CHECK(project.diagnostics.ok() && project.diagnostics.warning_count() == 0);
    CHECK(project.document.elements_by_tag("window").size() == 2);
}

} // namespace

void test_code_buffer() {
    grove::CodeBuffer b("int main() {\n    return 0;\n}");
    CHECK(b.line_count() == 3);
    b.set_caret(grove::CodePos{ 0, 12 });
    b.new_line(4, true);
    CHECK(b.line(1) == "    ");
    CHECK(b.caret().line == 1 && b.caret().column == 4);
    b.insert("x");
    CHECK(b.line(1) == "    x");
    b.backspace();
    b.backspace();
    CHECK(b.line(1) == "   ");
    CHECK(b.undo());
    CHECK(b.line(1) == "    x");
    CHECK(b.undo());
    CHECK(b.line(1) == "    ");
    CHECK(b.undo());
    CHECK(b.line_count() == 3 && b.line(0) == "int main() {");
    CHECK(b.redo());
    CHECK(b.line_count() == 4);

    grove::CodeBuffer c("abc def");
    c.set_caret(grove::CodePos{ 0, 0 });
    c.move_right(true, true);
    CHECK(c.selected_text() == "abc ");
    c.insert("X");
    CHECK(c.text() == "Xdef");
    c.select_all();
    c.insert("one\ntwo");
    CHECK(c.line_count() == 2 && c.caret().line == 1 && c.caret().column == 3);

    grove::CodeBuffer d("a\nb\nc");
    d.select(grove::CodePos{ 0, 0 }, grove::CodePos{ 2, 1 });
    d.indent(4, true);
    CHECK(d.text() == "    a\n    b\n    c");
    d.outdent(4);
    CHECK(d.text() == "a\nb\nc");

    grove::CodeBuffer e("f(|)");
    e.set_caret(grove::CodePos{ 0, 2 });
    e.delete_forward();
    CHECK(e.text() == "f()");
    e.set_caret(grove::CodePos{ 0, 2 });
    e.new_line(4, true);
    CHECK(e.text() == "f(\n    \n)");
    CHECK(e.caret().line == 1 && e.caret().column == 4);

    grove::CodeBuffer u("h\xC3\xA9llo");
    u.set_caret(grove::CodePos{ 0, 1 });
    u.move_right(false);
    CHECK(u.caret().column == 3);
    u.backspace();
    CHECK(u.text() == "hllo");

    CHECK(grove::code_display_column("\tx", 1, 4) == 4);
    CHECK(grove::code_byte_column("\tx", 4, 4) == 1);
    CHECK(grove::code_expand_tabs("a\tb", 4) == "a   b");
}

void test_code_highlight() {
    grove::CodeLineState st;
    auto kinds = grove::highlight_code_line(grove::CodeLanguage::Cpp, "int x = 42; // hi", st);
    CHECK(!kinds.empty() && kinds.front().kind == grove::CodeTokenKind::BuiltinType);
    CHECK(kinds.back().kind == grove::CodeTokenKind::Comment);
    grove::highlight_code_line(grove::CodeLanguage::C, "/* open", st);
    CHECK(st.block_comment);
    auto closing = grove::highlight_code_line(grove::CodeLanguage::C, "still */ return", st);
    CHECK(!st.block_comment && closing.front().kind == grove::CodeTokenKind::Comment && closing.back().kind == grove::CodeTokenKind::ControlKeyword);
    grove::CodeLineState ws;
    auto wal = grove::highlight_code_line(grove::CodeLanguage::Walnut, "function main() -> int {", ws);
    CHECK(wal.front().kind == grove::CodeTokenKind::Keyword);
    grove::CodeLineState ms;
    auto markup = grove::highlight_code_line(grove::CodeLanguage::Grove, "<p class=\"a\">hi</p>", ms);
    bool tag = false;
    bool attr = false;
    for (const auto& t : markup) {
        tag = tag || t.kind == grove::CodeTokenKind::Tag;
        attr = attr || t.kind == grove::CodeTokenKind::Attribute;
    }
    CHECK(tag && attr);
    CHECK(grove::code_language_for_path("/x/y/main.WAL") == grove::CodeLanguage::Walnut);
    CHECK(grove::code_language_for_path("theme.bark") == grove::CodeLanguage::Bark);
}

void test_multi_cursor() {
    grove::CodeBuffer buf("int a;\nint b;\nint c;");
    buf.set_caret(grove::CodePos{ 0, 4 });
    CHECK(buf.toggle_cursor(grove::CodePos{ 2, 4 }));
    CHECK(buf.cursor_count() == 2);
    buf.insert("x");
    CHECK(buf.text() == "int xa;\nint b;\nint xc;");
    buf.backspace();
    CHECK(buf.text() == "int a;\nint b;\nint c;");
    CHECK(buf.undo() && buf.text() == "int xa;\nint b;\nint xc;");
    CHECK(buf.cursor_count() == 2);
    buf.clear_extra_cursors();
    CHECK(buf.cursor_count() == 1);

    grove::CodeBuffer col("abcd\nab\nabcd");
    col.column_select(grove::CodePos{ 0, 3 }, 2);
    CHECK(col.cursor_count() == 3);
    col.insert("|");
    CHECK(col.text() == "abc|d\nab|\nabc|d");
    col.move_left(true);
    CHECK(col.any_selection() && col.selected_texts() == "|\n|\n|");
    col.insert_each({ "1", "2", "3" });
    CHECK(col.text() == "abc1d\nab2\nabc3d");
    col.move_start(false);
    CHECK(col.cursor_count() == 1);

    grove::CodeBuffer merge("hello");
    merge.set_caret(grove::CodePos{ 0, 2 });
    merge.toggle_cursor(grove::CodePos{ 0, 3 });
    merge.move_left(false);
    merge.move_left(false);
    merge.move_left(false);
    CHECK(merge.cursor_count() == 1);
    merge.toggle_cursor(merge.caret());
    CHECK(merge.cursor_count() == 1);
}

void test_pages_and_modals() {
    grove::set_virtual_files({
        { "/app/main.grove", "<grove><body><row id=\"tabs\"><button id=\"go-home\" to=\"home\">Home</button><button id=\"go-about\" to=\"pages/about.grove\">About</button></row>"
                             "<page id=\"home\" title=\"Home\"><p>Home</p><button id=\"ask\" to=\"confirm\">Ask</button></page>"
                             "<page id=\"about\" src=\"pages/about.grove\" />"
                             "<modal id=\"confirm\"><p>Sure?</p><button id=\"no\" to=\"close\">No</button></modal></body></grove>" },
        { "/app/pages/about.grove", "<grove><head><title>About us</title><style>p { color: red; }</style></head><body><p id=\"about-text\">About</p><img src=\"logo.png\" /><page id=\"deep\"><p>Deep</p></page></body></grove>" },
        { "/app/pages/logo.png", "x" },
    });
    grove::Project project = grove::load_project("/app/main.grove");
    grove::set_virtual_files({});
    grove::Document& doc = project.document;
    CHECK(project.diagnostics.ok());
    grove::Node* home = doc.find_by_id("home");
    grove::Node* about = doc.find_by_id("about");
    CHECK(home && about && doc.find_by_id("about-text") && doc.find_by_id("about-text")->parent() == about);
    CHECK(about && about->attribute_or("title", "") == "About us");
    const grove::Node* img = doc.first_by_tag("img");
    CHECK(img && img->attribute_or("src", "") == "pages/logo.png");
    CHECK(grove::is_page_current(*home) && !grove::is_page_current(*about));
    CHECK(grove::is_navigation_current(*doc.find_by_id("go-home")) && !grove::is_navigation_current(*doc.find_by_id("go-about")));
    CHECK(project.styles.style_of(*about, project.diagnostics).text("display").value_or("") == "none");

    int shown = 0;
    doc.events().add_listener(about, "page_show", [&shown](grove::Event&) { ++shown; });
    CHECK(grove::navigate(doc, "pages/about.grove"));
    CHECK(shown == 1 && grove::is_page_current(*about) && !grove::is_page_current(*home));
    CHECK(grove::is_navigation_current(*doc.find_by_id("go-about")));
    project.styles.invalidate();
    CHECK(project.styles.style_of(*about, project.diagnostics).text("display").value_or("") != "none");
    CHECK(project.styles.style_of(*home, project.diagnostics).text("display").value_or("") == "none");
    grove::Node* deep = doc.find_by_id("deep");
    CHECK(deep && grove::is_page_shown(*deep));
    CHECK(grove::go_back(doc) && grove::is_page_current(*home));
    CHECK(!grove::go_back(doc));
    CHECK(grove::navigate(doc, "#deep") && grove::is_page_current(*about) && grove::is_page_shown(*deep));

    grove::Node* modal = doc.find_by_id("confirm");
    int closed = 0;
    doc.events().add_listener(modal, "close", [&closed](grove::Event&) { ++closed; });
    CHECK(grove::navigate(doc, "confirm") && modal->has_attribute("open"));
    CHECK(grove::open_modals(doc.root()).size() == 1);
    CHECK(grove::navigate(doc, "close", doc.find_by_id("no")) && !modal->has_attribute("open") && closed == 1);
    CHECK(!grove::close_modal(doc, *modal));
    CHECK(grove::open_modal(doc, *modal));
    grove::Node* jump = doc.create_element("button");
    doc.append_child(*modal, *jump);
    CHECK(grove::navigate(doc, "home", jump) && grove::is_page_current(*home) && !modal->has_attribute("open"));
    CHECK(!grove::navigate(doc, "nowhere"));

    grove::set_virtual_files({ { "/loop/a.grove", "<grove><body><page src=\"a.grove\" /></body></grove>" } });
    grove::Project loop = grove::load_project("/loop/a.grove");
    grove::set_virtual_files({});
    CHECK(!loop.diagnostics.ok());
    CHECK(grove::is_known_tag("container") && grove::is_known_tag("page") && grove::is_known_tag("modal"));
}

void test_alignment_and_math() {
    grove::Diagnostics d;
    const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(
        ".a { width: calc(100% - 40px); height: calc((50% + 10px) * 2); }\n"
        ".b { width: min(100%, 300px); height: max(2em, 10px); margin: clamp(4px, 10vw, 40px); }\n"
        ".c { width: calc(100% -16px); }\n",
        "t.bark", d);
    CHECK(d.ok());
    const grove::bark::Viewport view{ 1000.0, 800.0 };
    auto value_of = [&](std::size_t rule, std::size_t decl) -> const grove::bark::Value& { return sheet.rules()[rule].declarations[decl].values.front(); };
    CHECK(grove::bark::evaluate_length(value_of(0, 0), 16.0, view, 200.0) == 160.0);
    CHECK(grove::bark::evaluate_length(value_of(0, 1), 16.0, view, 100.0) == 120.0);
    CHECK(grove::bark::evaluate_length(value_of(1, 0), 16.0, view, 500.0) == 300.0);
    CHECK(grove::bark::evaluate_length(value_of(1, 0), 16.0, view, 200.0) == 200.0);
    CHECK(grove::bark::evaluate_length(value_of(1, 1), 16.0, view, std::nullopt) == 32.0);
    CHECK(grove::bark::evaluate_length(value_of(1, 2), 16.0, view, std::nullopt) == 40.0);
    CHECK(grove::bark::evaluate_length(value_of(2, 0), 16.0, view, 100.0) == 84.0);
    CHECK(!grove::bark::evaluate_length(value_of(0, 0), 16.0, view, std::nullopt));
    CHECK(grove::bark::to_string(value_of(1, 0)) == "min(100%, 300px)");

    grove::Diagnostics bad;
    grove::bark::parse_stylesheet(".x { width: calc(100% +); }\n.y { width: clamp(1px, 2px); }\n", "bad.bark", bad);
    CHECK(bad.error_count() == 2);
    grove::Diagnostics modern;
    const auto colors = grove::bark::parse_stylesheet(".z { color: rgb(255 0 0 / 50%); }", "c.bark", modern);
    CHECK(modern.ok() && colors.rules()[0].declarations[0].values.front().is_color() && colors.rules()[0].declarations[0].values.front().color.a == 128);

    std::string h;
    std::string v;
    grove::bark::split_align({ grove::bark::Value::make_ident("center") }, h, v);
    CHECK(h == "center" && v == "center");
    grove::bark::split_align({ grove::bark::Value::make_ident("bottom"), grove::bark::Value::make_ident("right") }, h, v);
    CHECK(h == "right" && v == "bottom");
    grove::bark::split_align({ grove::bark::Value::make_ident("center"), grove::bark::Value::make_ident("left") }, h, v);
    CHECK(h == "left" && v == "center");
    grove::bark::split_align({ grove::bark::Value::make_ident("top"), grove::bark::Value::make_ident("center") }, h, v);
    CHECK(h == "center" && v == "top");

    grove::Project project = grove::load_project_from_source(
        "<grove><head><style>.card { align: bottom right; } .keep { align: center; text-align: left; } p { text-align: end; }</style></head><body>"
        "<div id=\"card\" class=\"card\"><p id=\"inner\">x</p></div><div id=\"keep\" class=\"keep\"></div>"
        "<div id=\"attr\" center></div><div id=\"words\" align=\"Top Right\"></div><center id=\"c\"></center></body></grove>",
        "t.grove", ".");
    CHECK(project.diagnostics.ok());
    auto computed = [&](const char* id, const char* prop) { return project.styles.style_of(*project.document.find_by_id(id), project.diagnostics).text(prop).value_or(""); };
    CHECK(computed("card", "text-align") == "right");
    CHECK(computed("inner", "text-align") == "right");
    CHECK(computed("keep", "text-align") == "left");
    CHECK(computed("attr", "align") == "center" && computed("attr", "text-align") == "center");
    CHECK(computed("words", "align") == "top right" && computed("words", "text-align") == "right");
    CHECK(computed("c", "align") == "center" && computed("c", "flex") == "1");
    CHECK(grove::bark::is_known_property("align") && grove::bark::is_known_property("align-items") && grove::bark::is_known_property("align-self"));
}

void test_color_formats() {
    const grove::bark::Rgba teal{ 60, 154, 138, 255 };
    CHECK(grove::format_color(teal, "rgb") == "rgb(60, 154, 138)");
    CHECK(grove::format_color(teal, "rgba") == "rgba(60, 154, 138, 1)");
    CHECK(grove::format_color(teal, "hsl") == "hsl(170, 44%, 42%)");
    CHECK(grove::format_color(grove::bark::Rgba{ 255, 0, 0, 128 }, "hsl") == "hsla(0, 100%, 50%, 0.5)");
    const auto parsed = grove::parse_color_text("rgba(255, 0, 0, 0.5)");
    CHECK(parsed && parsed->r == 255 && parsed->a == 128);
    const auto hsl = grove::parse_color_text("hsl(120, 100%, 50%)");
    CHECK(hsl && hsl->g == 255 && hsl->r == 0);
    const auto bare = grove::parse_color_text("ff8800");
    CHECK(bare && bare->r == 255 && bare->g == 136);
    CHECK(!grove::parse_color_text("rgb(1, 2)"));
    CHECK(!grove::parse_color_text("#zz0000"));
    const grove::ColorHsv hsv = grove::color_to_hsv(teal);
    const grove::bark::Rgba back = grove::color_from_hsv(hsv);
    CHECK(back.r == teal.r && back.g == teal.g && back.b == teal.b);

    grove::Document doc;
    grove::Node* c = doc.create_element("input_color");
    c->set_attribute("value", "#3c9a8a");
    c->set_attribute("format", "rgb");
    doc.append_child(doc.root(), *c);
    CHECK(grove::input_text(*c) == "rgb(60, 154, 138)");
    CHECK(grove::input_accepts(*c, "", "r") && grove::input_accepts(*c, "rgb", "("));
    CHECK(grove::commit_input_text(doc, *c, "hsla(0, 100%, 50%, 1)"));
    CHECK(grove::input_color(*c) && grove::input_color(*c)->r == 255);
    CHECK(!grove::set_clipboard_text(doc, "nothing to copy to without a window"));
}

void test_code_highlight_languages() {
    auto has = [](const std::vector<grove::CodeToken>& tokens, const std::string& line, grove::CodeTokenKind kind, const std::string& text) {
        for (const auto& t : tokens) {
            if (t.kind == kind && line.substr(t.begin, t.end - t.begin) == text) return true;
        }
        return false;
    };
    grove::CodeLineState st;
    const std::string cpp = "std::vector<std::string> v = {\"a\\n\"}; [[nodiscard]] int f();";
    auto tokens = grove::highlight_code_line(grove::CodeLanguage::Cpp, cpp, st);
    CHECK(has(tokens, cpp, grove::CodeTokenKind::Namespace, "std"));
    CHECK(has(tokens, cpp, grove::CodeTokenKind::Type, "vector"));
    CHECK(has(tokens, cpp, grove::CodeTokenKind::Escape, "\\n"));
    CHECK(has(tokens, cpp, grove::CodeTokenKind::Attribute, "nodiscard"));
    CHECK(has(tokens, cpp, grove::CodeTokenKind::Function, "f"));
    grove::highlight_code_line(grove::CodeLanguage::Cpp, "auto s = R\"x(first", st);
    CHECK(st.raw_string && st.raw_delimiter == "x");
    const std::string after = "second)x\"; return 0;";
    auto closing = grove::highlight_code_line(grove::CodeLanguage::Cpp, after, st);
    CHECK(!st.raw_string && closing.front().kind == grove::CodeTokenKind::String && has(closing, after, grove::CodeTokenKind::ControlKeyword, "return"));
    const std::string define = "#define LIMIT 10";
    CHECK(has(grove::highlight_code_line(grove::CodeLanguage::C, define, st), define, grove::CodeTokenKind::Constant, "LIMIT"));

    grove::CodeLineState cm;
    const std::string line = "if(NOT WIN32) message(STATUS \"${PROJECT_NAME} ok\") # note";
    auto cmake = grove::highlight_code_line(grove::CodeLanguage::CMake, line, cm);
    CHECK(has(cmake, line, grove::CodeTokenKind::Keyword, "if"));
    CHECK(has(cmake, line, grove::CodeTokenKind::Keyword, "NOT"));
    CHECK(has(cmake, line, grove::CodeTokenKind::Function, "message"));
    CHECK(has(cmake, line, grove::CodeTokenKind::Attribute, "STATUS"));
    CHECK(has(cmake, line, grove::CodeTokenKind::Variable, "${PROJECT_NAME}"));
    CHECK(cmake.back().kind == grove::CodeTokenKind::Comment);
    grove::highlight_code_line(grove::CodeLanguage::CMake, "#[[ long", cm);
    CHECK(cm.bracket_level == 0 && cm.bracket_comment);
    grove::highlight_code_line(grove::CodeLanguage::CMake, "comment ]] set(A 1)", cm);
    CHECK(cm.bracket_level < 0);
    CHECK(grove::code_language_for_path("/a/CMakeLists.txt") == grove::CodeLanguage::CMake);
    CHECK(grove::code_language_for_path("tools.cmake") == grove::CodeLanguage::CMake);
    CHECK(grove::code_language_from_name("cmake") == grove::CodeLanguage::CMake);
    CHECK(grove::code_token_kind_from_name("namespace") == grove::CodeTokenKind::Namespace);
    CHECK(!grove::code_token_kind_from_name("nope"));
    CHECK(grove::code_token_kinds().size() == 27 && grove::code_languages().size() == 17);
    CHECK(grove::bark::is_known_property("code-keyword") && grove::bark::is_known_property("code-mark"));
}

void test_walnut_highlight_detail() {
    using K = grove::CodeTokenKind;
    auto kind_of = [](const std::vector<grove::CodeToken>& tokens, const std::string& line, const std::string& text, std::size_t nth = 0) {
        std::size_t from = 0;
        std::size_t at = std::string::npos;
        for (std::size_t k = 0; k <= nth; ++k) {
            at = line.find(text, from);
            if (at == std::string::npos) return K::Plain;
            from = at + 1;
        }
        for (const auto& t : tokens) {
            if (t.begin <= at && at < t.end) return t.end >= at + text.size() ? t.kind : K::Plain;
        }
        return K::Plain;
    };
    grove::CodeLineState st;
    const std::string a = "    const int: digits = float_traits<T>::digits;";
    auto ta = grove::highlight_code_line(grove::CodeLanguage::Walnut, a, st);
    CHECK(kind_of(ta, a, "const") == K::Keyword);
    CHECK(kind_of(ta, a, "int") == K::BuiltinType);
    CHECK(kind_of(ta, a, "digits") == K::Declaration);
    CHECK(kind_of(ta, a, "float_traits") == K::TemplateScope);
    CHECK(kind_of(ta, a, "T") == K::Type);
    CHECK(kind_of(ta, a, "digits", 1) == K::Variable);
    CHECK(kind_of(ta, a, "=") == K::BuiltinOperator);
    const std::string b = "    if (times > LIMIT) then { return static_cast<T>(0); }";
    auto tb = grove::highlight_code_line(grove::CodeLanguage::Walnut, b, st);
    CHECK(kind_of(tb, b, "if") == K::ControlKeyword && kind_of(tb, b, "then") == K::ControlKeyword && kind_of(tb, b, "return") == K::ControlKeyword);
    CHECK(kind_of(tb, b, "static_cast") == K::Cast);
    CHECK(kind_of(tb, b, ">") == K::BuiltinOperator);
    const std::string c = "bool: ok = true && this != nullptr && self.x;";
    auto tc = grove::highlight_code_line(grove::CodeLanguage::Walnut, c, st);
    CHECK(kind_of(tc, c, "this") == K::ThisKeyword && kind_of(tc, c, "self") == K::SelfKeyword && kind_of(tc, c, "true") == K::Constant);
    const std::string d = "operator(+) function(const Vec&: other) const -> Vec {";
    auto td = grove::highlight_code_line(grove::CodeLanguage::Walnut, d, st);
    CHECK(kind_of(td, d, "(+)") == K::OverloadedOperator && kind_of(td, d, "other") == K::Declaration && kind_of(td, d, "->") == K::BuiltinOperator);
    const std::string e = "} end function scale";
    auto te = grove::highlight_code_line(grove::CodeLanguage::Walnut, e, st);
    CHECK(kind_of(te, e, "end") == K::ControlKeyword && kind_of(te, e, "function") == K::Keyword && kind_of(te, e, "scale") == K::Function);
    const std::string f = "namespace geo { std::io::println(`a ${x} \\t";
    auto tf = grove::highlight_code_line(grove::CodeLanguage::Walnut, f, st);
    CHECK(kind_of(tf, f, "geo") == K::Namespace && kind_of(tf, f, "io") == K::Namespace && kind_of(tf, f, "println") == K::Function);
    CHECK(kind_of(tf, f, "x") == K::Variable && kind_of(tf, f, "\\t") == K::Escape && st.string_open);
    auto tg = grove::highlight_code_line(grove::CodeLanguage::Walnut, "still` /* a /* b */", st);
    CHECK(!st.string_open && tg.front().kind == K::String && st.comment_depth == 1);
    grove::highlight_code_line(grove::CodeLanguage::Walnut, "c */ int", st);
    CHECK(st.comment_depth == 0);
    CHECK(grove::code_token_fallback(K::ControlKeyword) == K::Keyword && grove::code_token_fallback(K::EnumMember) == K::Constant);
    CHECK(grove::code_token_kind_from_name("built-in type") == K::BuiltinType);
    CHECK(grove::code_token_kind_from_name("control keyword") == K::ControlKeyword);
    CHECK(grove::code_token_kinds_for(grove::CodeLanguage::Walnut).size() == 23);

    const std::vector<std::string> lines = { "int: a = b;", "", "int: a = b;" };
    const std::string spans = grove::code_spans_text(lines, { { 0, 5, 1, K::Declaration }, { 0, 9, 1, K::Unresolved }, { 2, 9, 1, K::EnumMember } });
    const std::string problems = grove::code_problems_text(lines, { { 0, 9, 1, grove::CodeProblemKind::Error }, { 2, 0, 3, grove::CodeProblemKind::Warning } });
    grove::CodeOverlay overlay;
    overlay.set_spans(spans);
    overlay.set_problems(problems);
    CHECK(overlay.spans(0, lines[0]).size() == 2 && overlay.spans(2, lines[2]).size() == 1 && overlay.spans(3, lines[2]).front().kind == K::EnumMember);
    CHECK(overlay.spans(0, "int: a = c;").empty());
    CHECK(overlay.has_error(1, lines[0]) && !overlay.has_error(2, lines[2]) && overlay.problems(2, lines[2]).front().kind == grove::CodeProblemKind::Warning);
    grove::CodeLineState fresh;
    auto base = grove::highlight_code_line(grove::CodeLanguage::Walnut, lines[0], fresh);
    auto merged = grove::overlay_code_tokens(base, overlay.spans(0, lines[0]), lines[0].size());
    CHECK(kind_of(merged, lines[0], "b") == K::Unresolved && kind_of(merged, lines[0], "int") == K::BuiltinType);
    CHECK(grove::bark::is_known_property("code-control-keyword") && grove::bark::is_known_property("code-error-line"));

    const auto colors = grove::find_code_colors("#add .row:hover { color: #ff0000; border-color: rgba(0, 0, 255, 0.5) } // #fff");
    CHECK(colors.size() == 2 && colors[0].format == "hex" && colors[0].color.r == 255 && colors[1].format == "rgba" && colors[1].color.b == 255 && colors[1].color.a < 200);
    CHECK(grove::find_code_colors("#scores .row { }").empty() && grove::find_code_colors("$accent = #89b4fa;").size() == 1);
    CHECK(grove::find_code_colors("<p style=\"background: hsl(120, 100%, 50%)\">").size() == 1 && grove::bark::is_known_property("code-hover"));
}

void test_input_character_rules() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup("<body><input_string id=\"price\" block=\"letters special spaces\" /><input_string id=\"name\" allow=\"letters spaces\" chars=\"'-\" /><input_string id=\"code\" pattern=\"[A-Z]{3}-\\d{4}\" value=\"ABC-1234\" /><input_string id=\"bad\" pattern=\"(oops\" allow=\"lettres\" /></body>", "t.grove", d);
    CHECK(d.ok());
    grove::Node* price = doc.find_by_id("price");
    grove::Node* name = doc.find_by_id("name");
    grove::Node* code = doc.find_by_id("code");
    CHECK(price && name && code);
    CHECK(grove::input_accepts(*price, "3", "1") && grove::input_accepts(*price, "3", ".") && !grove::input_accepts(*price, "3", "a") && !grove::input_accepts(*price, "3", "$") && !grove::input_accepts(*price, "3", " "));
    CHECK(grove::input_accepts(*name, "", "\xC3\xA9") && grove::input_accepts(*name, "", "'") && !grove::input_accepts(*name, "", "7"));
    CHECK(!grove::commit_input_text(doc, *code, "AB-12") && grove::input_string(*code) == "ABC-1234");
    CHECK(grove::commit_input_text(doc, *code, "XYZ-0001") && grove::input_string(*code) == "XYZ-0001");
    CHECK(grove::input_charset(*price).describe().find("letters") != std::string::npos);
    grove::Diagnostics checks;
    grove::check_inputs(doc, checks);
    bool pattern_error = false;
    bool kind_warning = false;
    for (const auto& item : checks.items()) {
        pattern_error = pattern_error || item.message.find("never closed") != std::string::npos;
        kind_warning = kind_warning || item.message.find("lettres") != std::string::npos;
    }
    CHECK(pattern_error && kind_warning);
    CHECK(grove::char_kind_from_name("special") == fizmo::CharKind::special && !grove::char_kind_from_name("nope"));
}

void test_code_and_file_inputs() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup(
        "<body>\n"
        "  <input_code id=\"ed\" language=\"cpp\">\n"
        "      int main() {\n"
        "          return 0 < 1;\n"
        "      }\n"
        "  </input_code>\n"
        "  <input_file id=\"pick\" mode=\"folder\">Open folder</input_file>\n"
        "  <input_file id=\"bad\" mode=\"sideways\" />\n"
        "</body>\n",
        "t.grove", d);
    CHECK(d.ok());
    grove::check_inputs(doc, d);
    CHECK(d.error_count() == 1);
    grove::Node* ed = doc.find_by_id("ed");
    CHECK(ed && grove::input_kind_of(*ed) == grove::InputKind::Code);
    CHECK(ed && grove::input_string(*ed).value_or("") == "int main() {\n    return 0 < 1;\n}");
    CHECK(ed && grove::set_input_string(doc, *ed, "x"));
    CHECK(ed && grove::input_text(*ed) == "x");
    grove::Node* pick = doc.find_by_id("pick");
    CHECK(pick && grove::input_kind_of(*pick) == grove::InputKind::File);
    CHECK(pick && grove::input_string(*pick).value_or("?").empty());
    CHECK(grove_input_kind_of(reinterpret_cast<const grove_element*>(pick)) == GROVE_INPUT_FILE);
}

void test_natives() {
    grove::Document doc;
    CHECK(!grove::has_native(doc, "add"));
    grove::register_native(doc, "add", [](const std::vector<std::string>& args) {
        long total = 0;
        for (const std::string& a : args) total += std::stol(a);
        return std::to_string(total);
    });
    grove::register_native(doc, "boom", [](const std::vector<std::string>&) -> std::string { throw std::runtime_error("nope"); });
    CHECK(grove::has_native(doc, "add"));
    CHECK(grove::call_native(doc, "add", { "2", "40" }).value_or("") == "42");
    std::string error;
    doc.events().set_error_handler([&](const std::string& m) { error = m; });
    CHECK(grove::call_native(doc, "boom", {}).value_or("?").empty());
    CHECK(error.find("nope") != std::string::npos);
    CHECK(!grove::call_native(doc, "missing", {}));
    grove_document* handle = reinterpret_cast<grove_document*>(&doc);
    const char* args[] = { "1", "2", "3" };
    const char* r = grove_call_native(handle, "add", args, 3);
    CHECK(r && std::string(r) == "6");
    CHECK(grove_has_native(handle, "add"));
    CHECK(grove_call_native(handle, "missing", nullptr, 0) == nullptr);
    doc.host().file_dialog = [](const grove::FileDialogRequest& req) {
        return std::vector<std::string>{ "/picked/" + req.mode, "/second" };
    };
    CHECK(std::string(grove_file_dialog(handle, "folder", "Pick", "", true)) == "/picked/folder\n/second");
    CHECK(grove::unregister_native(doc, "add") && !grove::has_native(doc, "add"));
}

void test_images() {
    const std::string dir = std::string(GROVE_EXAMPLES_DIR) + "/editor";
    grove::Project p = grove::load_project_from_source("<body icon=\"../../Tools/icons/grove-32.png missing-icon.png\"><img src=\"nope.png\" /><img src=\"../../Tools/icons/bark-16.png\" /></body>", dir + "/t.grove", dir);
    CHECK(p.diagnostics.warning_count() == 2);
    const grove::Node* body = p.document.first_by_tag("body");
    CHECK(body && grove::icon_paths(p, *body).size() == 2);
    CHECK(grove::split_asset_list(" a.png, b.png\tc.png ").size() == 3);
}

void test_zoom_and_select() {
    grove::Diagnostics d;
    grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(
        "body { padding: 10px; font-size: 12pt; zoom: 2; opacity: 0.5; }\n"
        "p { font-size: 1.5em; margin: 2vmin; }\n",
        "t.bark", d);
    CHECK(d.ok());
    grove::Document doc = grove::parse_markup("<body><p id=\"p\">x</p></body>", "t.grove", d);
    grove::bark::StyleEngine engine;
    engine.add(sheet);
    engine.set_viewport(grove::bark::Viewport{ 800, 600, 2.0 });
    const auto& body = engine.style_of(*doc.first_by_tag("body"), d);
    CHECK(body.length("padding") && *body.length("padding") == 20.0);
    CHECK(body.font_size() == 32.0);
    CHECK(body.length("opacity") && *body.length("opacity") == 0.5);
    const auto& p = engine.style_of(*doc.find_by_id("p"), d);
    CHECK(p.font_size() == 48.0);
    CHECK(p.length("margin") && *p.length("margin") == 12.0);
    CHECK(grove::bark::is_known_property("zoom") && grove::bark::is_known_property("overflow-x"));

    grove::Document form = grove::parse_markup(
        "<body>\n"
        "  <input_select id=\"size\" value=\"m\">\n"
        "    <option value=\"s\">Small</option>\n"
        "    <option value=\"m\">Medium</option>\n"
        "    <option>Large</option>\n"
        "  </input_select>\n"
        "  <input_select id=\"empty\" placeholder=\"Pick one\"><option>One</option></input_select>\n"
        "  <input_select id=\"bad\" value=\"x\"><option>One</option></input_select>\n"
        "</body>\n",
        "t.grove", d);
    CHECK(d.ok());
    grove::Diagnostics checks;
    grove::check_inputs(form, checks);
    CHECK(checks.error_count() == 0 && checks.warning_count() == 1);
    grove::Node* size = form.find_by_id("size");
    CHECK(size && grove::input_kind_of(*size) == grove::InputKind::Select);
    CHECK(size && grove::input_string(*size).value_or("") == "m");
    const std::vector<grove::SelectOption> options = grove::select_options(*size);
    CHECK(options.size() == 3 && options[2].value == "Large" && options[0].label == "Small");
    CHECK(size && grove::select_index(*size).value_or(9) == 1);
    CHECK(size && grove::set_input_string(form, *size, "s") && grove::select_index(*size).value_or(9) == 0);
    grove::Node* empty = form.find_by_id("empty");
    CHECK(empty && !grove::select_index(*empty) && grove::input_string(*empty).value_or("?").empty());
    const std::vector<grove::SelectOption> picks = { { "a", "A", false }, { "b", "B", false } };
    CHECK(empty && grove::set_select_options(form, *empty, picks) && grove::select_options(*empty).size() == 2);
    CHECK(grove_input_kind_of(reinterpret_cast<const grove_element*>(size)) == GROVE_INPUT_SELECT);
    CHECK(grove::input_kind_from_name("select") == grove::InputKind::Select);
}

void test_keymap() {
    const auto c = grove::parse_chord("ctrl+shift+m");
    CHECK(c && c->ctrl && c->shift && !c->alt && c->key == "M" && grove::chord_text(*c) == "Ctrl+Shift+M");
    CHECK(grove::parse_chord("Ctrl+,") && grove::parse_chord("Ctrl+,")->key == ",");
    CHECK(grove::parse_chord("Ctrl++") && grove::parse_chord("Ctrl++")->key == "=");
    CHECK(grove::parse_chord("Alt+LeftArrow")->key == "Left" && grove::parse_chord("f5")->key == "F5");
    CHECK(!grove::parse_chord("Ctrl+Banana") && !grove::parse_chord("Hyper+A"));
    CHECK(grove::parse_chord("Alt+Click")->mouse() && !grove::parse_chord("Alt+C")->mouse());
    const auto many = grove::parse_chords("Ctrl+Y, Ctrl+Shift+Z, Ctrl+,");
    CHECK(many.size() == 3 && many[2].key == ",");
    CHECK(grove::chord_of("Comma", grove::ModifierMeta) == *grove::parse_chord("Ctrl+,"));
    const grove::Keymap map = grove::parse_keymap("copy=Ctrl+Shift+C; redo=none; add-caret=Ctrl+Alt+Click");
    CHECK(grove::code_action_for(map, *grove::parse_chord("Ctrl+Shift+C")).value_or("") == "copy");
    CHECK(!grove::code_action_for(map, *grove::parse_chord("Ctrl+C")) && grove::is_code_default_chord(*grove::parse_chord("Ctrl+C")));
    CHECK(!grove::code_action_for(map, *grove::parse_chord("Ctrl+Y")));
    CHECK(grove::code_action_for(map, *grove::parse_chord("Ctrl+Alt+Click")).value_or("") == "add-caret");
    CHECK(grove::code_action_for({}, *grove::parse_chord("Ctrl+Click")).value_or("") == "go-to-definition");
    CHECK(grove::keymap_text(map) == "add-caret=Ctrl+Alt+Click; copy=Ctrl+Shift+C; redo=none");
}

void test_dump_options() {
    grove::Diagnostics d;
    const std::string path = "/home/runner/work/Canopy/Canopy/Tests/hello/world.cpp";
    const std::string words = "one two three four five six seven eight nine ten eleven twelve thirteen";
    grove::Document doc = grove::parse_markup("<body><item data-path=\"" + path + "\">" + words + "</item></body>", "t.grove", d);
    CHECK(d.ok());
    std::ostringstream shortened;
    grove::dump(doc.root(), shortened);
    CHECK(shortened.str().find(path) == std::string::npos && shortened.str().find("...") != std::string::npos);
    std::ostringstream full;
    grove::dump(doc.root(), full, grove::DumpOptions::full());
    CHECK(full.str().find("data-path=\"" + path + "\"") != std::string::npos);
    CHECK(full.str().find(words) != std::string::npos && full.str().find("...") == std::string::npos);
    std::ostringstream wider;
    grove::dump(doc.root(), wider, grove::DumpOptions{ 0, 100 });
    CHECK(wider.str().find(path) != std::string::npos);
}

void test_menus() {
    grove::Diagnostics d;
    grove::Document doc = grove::parse_markup("<body><list id=\"files\"></list></body>", "t.grove", d);
    CHECK(d.ok());
    const grove::MenuItem item = grove::menu_item("rename", "Rename", "F2", true);
    CHECK(item.value == "rename" && item.label == "Rename" && item.detail == "F2" && item.disabled && !item.separator);
    CHECK(grove::menu_separator().separator);
    grove::Node* files = doc.find_by_id("files");
    CHECK(files && !grove::open_menu(doc, *files, { item }, 10, 10));
    CHECK(!grove::close_menu(doc));
    bool asked = false;
    doc.host().open_menu = [&](grove::Node& owner, const std::vector<grove::MenuItem>& items, double x, double y) {
        asked = &owner == files && items.size() == 2 && x == 10 && y == 20;
        return true;
    };
    CHECK(files && grove::open_menu(doc, *files, { item, grove::menu_separator() }, 10, 20) && asked);
    CHECK(files && !grove::open_menu(doc, *files, {}, 10, 20));
}

void test_terminal_basics() {
    grove::Terminal t(10, 4);
    t.feed("hello");
    CHECK(t.row_text(0) == "hello" && t.cursor_column() == 5 && t.cursor_row() == 0);
    t.feed("\r\nworld\x1b[2;3Hxy");
    CHECK(t.row_text(1) == "woxyd" && t.cursor_column() == 4 && t.cursor_row() == 1);
    t.feed("\x1b[H\x1b[2J");
    CHECK(t.screen_text() == "\n\n\n\n" && t.cursor_row() == 0 && t.cursor_column() == 0);
    t.feed("0123456789AB");
    CHECK(t.row_text(0) == "0123456789" && t.row_text(1) == "AB" && t.line(0).wrapped);
    grove::Terminal w(5, 2);
    w.feed("abcde");
    CHECK(w.cursor_column() == 4 && w.row_text(1).empty());
    w.feed("\x08X");
    CHECK(w.row_text(0) == "abcXe" && w.row_text(1).empty());
    grove::Terminal e(10, 3);
    e.feed("abcdefghij\x1b[1;4H\x1b[K");
    CHECK(e.row_text(0) == "abc");
    e.feed("\x1b[1;1Hxyzw\x1b[1;3H\x1b[1K");
    CHECK(e.row_text(0) == "   w");
    e.feed("\x1b[2;1H12345\x1b[2;2H\x1b[2P");
    CHECK(e.row_text(1) == "145");
    e.feed("\x1b[2;2H\x1b[3@");
    CHECK(e.row_text(1) == "1   45");
    e.feed("\x1b[2;1H\x1b[3X");
    CHECK(e.row_text(1) == "    45");
    grove::Terminal tab(20, 2);
    tab.feed("a\tb\x1b[3gc\td");
    CHECK(tab.row_text(0) == "a       bc         d");
}

void test_terminal_scrolling() {
    grove::Terminal t(10, 3, 100);
    t.feed("1\r\n2\r\n3\r\n4\r\n5");
    CHECK(t.scrollback_size() == 2 && t.row_text(0) == "3" && t.row_text(2) == "5");
    CHECK(t.view_line(0).cells.size() == 10);
    t.scroll_view(2);
    CHECK(t.view_offset() == 2 && t.view_line(0).cells[0].ch == U'1');
    t.reset_view();
    grove::Terminal r(10, 5, 100);
    r.feed("a\r\nb\r\nc\r\nd\r\ne\x1b[2;4r\x1b[4;1H\n\n");
    CHECK(r.row_text(0) == "a" && r.row_text(1) == "d" && r.row_text(4) == "e" && r.scrollback_size() == 0);
    r.feed("\x1b[r\x1b[2;1H\x1bM\x1bM");
    CHECK(r.row_text(0).empty() && r.row_text(1) == "a" && r.cursor_row() == 0);
    grove::Terminal l(10, 4);
    l.feed("1\r\n2\r\n3\r\n4\x1b[2;1H\x1b[L");
    CHECK(l.row_text(1).empty() && l.row_text(2) == "2" && l.row_text(3) == "3");
    l.feed("\x1b[M\x1b[M");
    CHECK(l.row_text(1) == "3" && l.row_text(2).empty() && l.scrollback_size() == 0);
    grove::Terminal limit(5, 2, 3);
    for (int i = 0; i < 10; ++i) limit.feed(std::to_string(i) + "\r\n");
    CHECK(limit.scrollback_size() == 3 && limit.history_line(0).cells[0].ch == U'6');
}

void test_terminal_alternate_screen() {
    grove::Terminal t(10, 3, 100);
    t.feed("shell$ ");
    t.feed("\x1b[?1049h\x1b[H\x1b[2Jvim");
    CHECK(t.alternate_screen() && t.row_text(0) == "vim");
    for (int i = 0; i < 6; ++i) t.feed("\r\nline");
    CHECK(t.scrollback_size() == 0);
    t.feed("\x1b[?1049l");
    CHECK(!t.alternate_screen() && t.row_text(0) == "shell$" && t.cursor_column() == 7 && t.cursor_row() == 0);
    t.feed("\x1b[?1049h");
    CHECK(t.row_text(0).empty());
    t.feed("\x1b[?1049l");
}

void test_terminal_colors() {
    grove::Terminal t(20, 2);
    t.feed("\x1b[1;31mA\x1b[38;5;202mB\x1b[38;2;10;20;30mC\x1b[48:2::1:2:3mD\x1b[0;7mE\x1b[4:3mF\x1b[24;27mG");
    const auto& c = t.line(0).cells;
    CHECK((c[0].attrs & grove::TermBold) && c[0].fg == grove::TermColor::indexed(1));
    CHECK(c[1].fg == grove::TermColor::indexed(202));
    CHECK(c[2].fg == grove::TermColor::rgb(10, 20, 30));
    CHECK(c[3].bg == grove::TermColor::rgb(1, 2, 3) && c[3].fg == grove::TermColor::rgb(10, 20, 30));
    CHECK(c[4].attrs == grove::TermInverse && c[4].fg.kind == grove::TermColor::Kind::Default);
    CHECK((c[5].attrs & grove::TermCurlyUnderline) && (c[5].attrs & grove::TermInverse));
    CHECK(c[6].attrs == 0);
    t.feed("\x1b[44m\x1b[2;1H\x1b[K");
    CHECK(t.line(1).cells[5].bg == grove::TermColor::indexed(4));
    CHECK(t.resolve(grove::TermColor::indexed(196), true) == (grove::TermRgb{ 255, 0, 0 }));
    CHECK(t.resolve(grove::TermColor::indexed(244), true) == (grove::TermRgb{ 128, 128, 128 }));
    t.feed("\x1b]4;1;#ff8000\x07");
    CHECK(t.palette(1) == (grove::TermRgb{ 255, 128, 0 }));
    t.feed("\x1b]4;1;rgb:00/80/ff\x1b\\");
    CHECK(t.palette(1) == (grove::TermRgb{ 0, 128, 255 }));
}

void test_terminal_unicode() {
    grove::Terminal t(6, 2);
    t.feed("a\xE4\xBD\xA0" "b");
    const auto& c = t.line(0).cells;
    CHECK(c[1].ch == U'你' && c[1].width == 2 && c[2].width == 0 && c[3].ch == U'b' && t.cursor_column() == 4);
    t.feed("\r\ne\xCC\x81x");
    CHECK(t.line(1).cells[0].ch == U'e' && t.line(1).cells[0].marks == U"́" && t.line(1).cells[1].ch == U'x');
    grove::Terminal edge(4, 2);
    edge.feed("abc\xE4\xBD\xA0");
    CHECK(edge.row_text(0) == "abc" && edge.line(1).cells[0].width == 2 && edge.line(0).wrapped);
    grove::Terminal over(6, 1);
    over.feed("\xE4\xBD\xA0\xE5\xA5\xBD\x1b[1;2Hx");
    CHECK(over.line(0).cells[0].ch == U' ' && over.line(0).cells[1].ch == U'x' && over.line(0).cells[2].width == 2);
    grove::Terminal dec(10, 1);
    dec.feed("\x1b(0lqk\x1b(Bq");
    CHECK(dec.row_text(0) == "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90q");
    grove::Terminal split(5, 1);
    split.feed("\xE2\x82");
    split.feed("\xAC!");
    CHECK(split.line(0).cells[0].ch == U'€' && split.line(0).cells[1].ch == U'!');
    grove::Terminal bad(5, 1);
    bad.feed("\xFFz");
    CHECK(bad.line(0).cells[0].ch == 0xFFFD && bad.line(0).cells[1].ch == U'z');
    CHECK(grove::terminal_char_width(U'\U0001F600') == 2 && grove::terminal_char_width(U'​') == 0 && grove::terminal_char_width(U'é') == 1);
}

void test_terminal_replies_and_modes() {
    grove::Terminal t(80, 24);
    t.feed("\x1b[5;10H\x1b[6n\x1b[c\x1b[>c\x1b[5n");
    CHECK(t.take_reply() == "\x1b[5;10R\x1b[?62;22c\x1b[>1;10;0c\x1b[0n");
    t.feed("\x1b]11;?\x07");
    const std::string bg = t.take_reply();
    CHECK(bg.rfind("\x1b]11;rgb:", 0) == 0);
    t.feed("\x1b[?2004h\x1b[?1$p");
    CHECK(t.bracketed_paste() && t.take_reply() == "\x1b[?1;2$y");
    t.feed("\x1b[18t");
    CHECK(t.take_reply() == "\x1b[8;24;80t");
    t.feed("\x1bP$qr\x1b\\");
    CHECK(t.take_reply() == "\x1bP1$r1;24r\x1b\\");
    t.feed("\x1b]0;my title\x07");
    CHECK(t.title() == "my title" && t.take_title_changed());
    t.feed("\x1b]7;file://host/home/me/My%20Dir\x1b\\");
    CHECK(t.working_directory() == "/home/me/My Dir");
    t.feed("\x1b[?25l\x1b[6 q\x1b[?1h\x1b=");
    CHECK(!t.cursor_visible() && t.cursor_shape() == grove::TermCursorShape::Bar && t.application_cursor_keys() && t.application_keypad());
    t.feed("\x1b[!p");
    CHECK(t.cursor_visible() && !t.application_cursor_keys());
    t.feed("\x1b[3;10r\x1b[?6h\x1b[H");
    CHECK(t.cursor_row() == 2 && t.origin_mode());
    t.feed("\x1b[?6l\x1b[r\x1b[4h");
    CHECK(t.insert_mode());
    t.feed("\x1b[4l\x1b[?7l\x1b[1;79Habcd");
    CHECK(t.row_text(0).size() == 80 && t.line(0).cells[79].ch == U'd' && t.cursor_row() == 0);
    grove::Terminal unknown(10, 2);
    unknown.feed("a\x1b[?9999hb\x1b]999;x\x07" "c\x1bP+q544e\x1b\\d\x1b_apc\x1b\\e");
    CHECK(unknown.row_text(0) == "abcde");
}

void test_terminal_input_encoding() {
    grove::Terminal t(80, 24);
    CHECK(t.encode_key(grove::TermKey::Up, 0) == "\x1b[A");
    CHECK(t.encode_key(grove::TermKey::Up, grove::TermCtrl) == "\x1b[1;5A");
    CHECK(t.encode_key(grove::TermKey::Delete, 0) == "\x1b[3~" && t.encode_key(grove::TermKey::PageUp, grove::TermShift) == "\x1b[5;2~");
    CHECK(t.encode_key(grove::TermKey::F1, 0) == "\x1bOP" && t.encode_key(grove::TermKey::F5, 0) == "\x1b[15~" && t.encode_key(grove::TermKey::F12, grove::TermAlt) == "\x1b[24;3~");
    CHECK(t.encode_key(grove::TermKey::Backspace, 0) == "\x7f" && t.encode_key(grove::TermKey::Enter, 0) == "\r" && t.encode_key(grove::TermKey::Tab, grove::TermShift) == "\x1b[Z");
    CHECK(t.encode_char(U'c', grove::TermCtrl) == "\x03" && t.encode_char(U'x', grove::TermAlt) == "\x1bx" && t.encode_char(U'[', grove::TermCtrl) == "\x1b");
    CHECK(t.encode_char(U'é', 0) == "\xC3\xA9");
    t.feed("\x1b[?1h");
    CHECK(t.encode_key(grove::TermKey::Left, 0) == "\x1bOD" && t.encode_key(grove::TermKey::Home, 0) == "\x1bOH");
    CHECK(t.encode_paste("a\nb") == "a\rb");
    t.feed("\x1b[?2004h");
    CHECK(t.encode_paste("x\x1b[201~y") == "\x1b[200~x[201~y\x1b[201~");
    grove::TermMouse m;
    m.button = 0;
    m.column = 4;
    m.row = 2;
    CHECK(t.encode_mouse(m).empty());
    t.feed("\x1b[?1000h");
    CHECK(t.encode_mouse(m) == std::string("\x1b[M") + char(32) + char(37) + char(35));
    t.feed("\x1b[?1006h");
    CHECK(t.encode_mouse(m) == "\x1b[<0;5;3M");
    m.press = false;
    CHECK(t.encode_mouse(m) == "\x1b[<0;5;3m");
    m.press = true;
    m.motion = true;
    CHECK(t.encode_mouse(m).empty());
    t.feed("\x1b[?1002h");
    CHECK(t.encode_mouse(m) == "\x1b[<32;5;3M");
    CHECK(t.encode_wheel(true, 0, 0, 0) == "\x1b[<64;1;1M");
    grove::Terminal alt(10, 3);
    alt.feed("\x1b[?1049h");
    CHECK(alt.encode_wheel(false, 0, 0, 0) == "\x1b[B\x1b[B\x1b[B");
    CHECK(alt.encode_focus(true).empty());
    alt.feed("\x1b[?1004h");
    CHECK(alt.encode_focus(true) == "\x1b[I");
}

void test_terminal_selection_and_resize() {
    grove::Terminal t(10, 3, 100);
    t.feed("hello world\r\nnext line");
    t.select_word(grove::TermPoint{ 0, 2 });
    CHECK(t.selected_text() == "hello");
    t.select(grove::TermPoint{ 0, 6 }, grove::TermPoint{ 2, 4 });
    CHECK(t.selected_text() == "world\nnext");
    t.select_line(grove::TermPoint{ 1, 0 });
    CHECK(t.selected_text() == "hello world");
    grove::Terminal r(10, 4, 100);
    r.feed("1\r\n2\r\n3\r\n4");
    r.resize(10, 2);
    CHECK(r.rows() == 2 && r.row_text(0) == "3" && r.row_text(1) == "4" && r.scrollback_size() == 2 && r.cursor_row() == 1);
    r.resize(10, 4);
    CHECK(r.row_text(0) == "1" && r.row_text(3) == "4" && r.scrollback_size() == 0 && r.cursor_row() == 3);
    r.resize(5, 4);
    CHECK(r.columns() == 5 && r.line(0).cells.size() == 5);
    grove::Terminal blank(10, 5);
    blank.feed("$ ");
    blank.resize(10, 3);
    CHECK(blank.row_text(0) == "$" && blank.scrollback_size() == 0);
}

void test_completion() {
    CHECK(grove::completion_word_start("    std::vec", 12) == 9);
    CHECK(grove::completion_word_start("font-si", 7) == 5 && grove::completion_word_start("font-si", 7, "-") == 0);
    int score = -1;
    CHECK(grove::completion_match_positions("vector", "vec", score) && score == 0);
    CHECK(grove::completion_match_positions("Vector", "vec", score) && score == 1);
    auto camel = grove::completion_match_positions("getValueOr", "gvo", score);
    CHECK(camel && score == 2 && (*camel)[1] == 3 && (*camel)[2] == 8);
    auto snake = grove::completion_match_positions("push_back", "pb", score);
    CHECK(snake && score == 2 && (*snake)[1] == 5);
    CHECK(grove::completion_match_positions("vector", "vtr", score) && score == 3);
    CHECK(!grove::completion_match_positions("vector", "xyz", score));
    CHECK(!grove::completion_match_positions("vector", "tor", score));
    std::vector<grove::CompletionItem> items(4);
    items[0].label = "reserve";
    items[1].label = "resize";
    items[2].label = "Result";
    items[3].label = "rbegin";
    const auto ranked = grove::filter_completions(items, "res");
    CHECK(ranked.size() == 3 && ranked[0].index == 0 && ranked[1].index == 1 && ranked[2].index == 2);
    grove::CompletionList list;
    list.line = 3;
    list.column = 7;
    grove::CompletionItem tricky;
    tricky.label = "printf";
    tricky.kind = "function";
    tricky.detail = "int (const char*, ...)";
    tricky.insert = "printf($0)";
    tricky.documentation = "Prints.\nTab\there \\ done";
    list.items.push_back(tricky);
    const auto back = grove::parse_completions(grove::completions_text(list));
    CHECK(back && back->line == 3 && back->column == 7 && back->items.size() == 1);
    CHECK(back && back->items[0].documentation == tricky.documentation && back->items[0].insert == "printf($0)" && back->items[0].detail == tricky.detail);
    std::size_t caret = 0;
    CHECK(grove::completion_insert_text(tricky, caret) == "printf()" && caret == 7);
    grove::CompletionItem plain;
    plain.label = "size";
    CHECK(grove::completion_insert_text(plain, caret) == "size" && caret == 4);
    const std::vector<std::string> lines = { "int counter = 0;", "count", "counter += total_value;" };
    const auto words = grove::word_completions(lines, 1, 5);
    bool has_counter = false;
    bool has_count = false;
    bool has_total = false;
    for (const auto& w : words) {
        has_counter = has_counter || w.label == "counter";
        has_count = has_count || w.label == "count";
        has_total = has_total || w.label == "total_value";
    }
    CHECK(has_counter && has_total && !has_count);
    CHECK(!grove::parse_completions("") && !grove::parse_completions("nonsense"));
    bool has_class = false;
    bool has_int = false;
    for (const auto& w : grove::code_language_words(grove::CodeLanguage::Cpp)) {
        has_class = has_class || (w.word == "class" && w.kind == grove::CodeTokenKind::Keyword);
        has_int = has_int || (w.word == "int" && w.kind == grove::CodeTokenKind::BuiltinType);
    }
    CHECK(has_class && has_int && grove::code_language_words(grove::CodeLanguage::Plain).empty());
}

void test_code_find() {
    using grove::CodeFindOptions;
    const std::vector<std::string> lines = { "int Count = count + 1;", "recount(count);", "", "COUNT" };
    CodeFindOptions any;
    auto m = grove::find_code_matches(lines, "count", any);
    CHECK(m.size() == 5);
    CHECK(m[0].begin.line == 0 && m[0].begin.column == 4 && m[0].end.column == 9);
    CodeFindOptions exact;
    exact.match_case = true;
    CHECK(grove::find_code_matches(lines, "count", exact).size() == 3);
    CodeFindOptions word;
    word.whole_word = true;
    m = grove::find_code_matches(lines, "count", word);
    CHECK(m.size() == 4);
    CodeFindOptions re;
    re.regex = true;
    m = grove::find_code_matches(lines, "c(o)unt\\b", re);
    CHECK(m.size() == 5);
    CHECK(grove::code_replacement_for(lines, m[1], "c(o)unt\\b", "[$1$&]", re) == "[ocount]");
    bool bad = false;
    CHECK(grove::find_code_matches(lines, "(", re, &bad).empty() && bad);
    CHECK(grove::find_code_matches(lines, "", any).empty());
    CHECK(grove::find_code_matches({ "aaaa" }, "aa", any).size() == 2);
    m = grove::find_code_matches(lines, "count", any);
    CHECK(grove::code_match_at_or_after(m, grove::CodePos{ 1, 0 }) == 2);
    CHECK(grove::code_match_at_or_after(m, grove::CodePos{ 3, 3 }) == 0);
    CHECK(grove::code_match_before(m, grove::CodePos{ 0, 4 }) == 4);
    CHECK(grove::code_match_before(m, grove::CodePos{ 1, 0 }) == 1);
}

void test_more_languages() {
    using grove::CodeTokenKind;
    using grove::CodeLanguage;
    auto kinds = [](CodeLanguage lang, const std::vector<std::string>& lines) {
        grove::CodeLineState st;
        std::vector<std::vector<std::pair<std::string, CodeTokenKind>>> out;
        for (const std::string& l : lines) {
            std::vector<std::pair<std::string, CodeTokenKind>> row;
            for (const grove::CodeToken& t : grove::highlight_code_line(lang, l, st)) row.emplace_back(l.substr(t.begin, t.end - t.begin), t.kind);
            out.push_back(row);
        }
        return out;
    };
    auto has = [](const std::vector<std::pair<std::string, CodeTokenKind>>& row, const std::string& text, CodeTokenKind kind) {
        for (const auto& [t, k] : row) {
            if (t == text && k == kind) return true;
        }
        return false;
    };
    CHECK(grove::code_language_for_path("src/main.rs") == CodeLanguage::Rust && grove::code_language_for_path("a.zig") == CodeLanguage::Zig &&
          grove::code_language_for_path("x.py") == CodeLanguage::Python && grove::code_language_for_path("x.lua") == CodeLanguage::Lua &&
          grove::code_language_for_path("Main.java") == CodeLanguage::Java && grove::code_language_for_path("P.cs") == CodeLanguage::CSharp &&
          grove::code_language_for_path("boot.asm") == CodeLanguage::Assembly && grove::code_language_for_path("start.S") == CodeLanguage::Assembly);
    auto rs = kinds(CodeLanguage::Rust, { "fn main<'a>(x: &'a str) -> Vec<u8> { println!(\"{}\", x); }", "#[derive(Debug)] /* a /* b */", "still */ let s = r#\"raw \"q\"", "end\"#; 'c'" });
    CHECK(has(rs[0], "fn", CodeTokenKind::Keyword) && has(rs[0], "'a", CodeTokenKind::Attribute) && has(rs[0], "println!", CodeTokenKind::Preprocessor) &&
          has(rs[0], "Vec", CodeTokenKind::BuiltinType) && has(rs[0], "u8", CodeTokenKind::BuiltinType));
    CHECK(has(rs[1], "#[derive(Debug)]", CodeTokenKind::Attribute) && has(rs[2], "still */", CodeTokenKind::Comment) && has(rs[2], "let", CodeTokenKind::Keyword));
    CHECK(has(rs[3], "end\"#", CodeTokenKind::String) && has(rs[3], "'c'", CodeTokenKind::String));
    auto zig = kinds(CodeLanguage::Zig, { "const std = @import(\"std\");", "    \\\\ multi line", "pub fn main() !void { try x; }" });
    CHECK(has(zig[0], "@import", CodeTokenKind::Function) && has(zig[0], "const", CodeTokenKind::Keyword) && has(zig[1], "\\\\ multi line", CodeTokenKind::String));
    CHECK(has(zig[2], "void", CodeTokenKind::BuiltinType) && has(zig[2], "try", CodeTokenKind::ControlKeyword));
    auto py = kinds(CodeLanguage::Python, { "@dataclass", "def area(self, r=2):  # note", "    s = f'{r}' + \"\"\"doc", "more\"\"\"; return None" });
    CHECK(has(py[0], "@dataclass", CodeTokenKind::Attribute) && has(py[1], "area", CodeTokenKind::Function) && has(py[1], "self", CodeTokenKind::ThisKeyword) &&
          has(py[1], "# note", CodeTokenKind::Comment));
    CHECK(has(py[3], "more\"\"\"", CodeTokenKind::String) && has(py[3], "return", CodeTokenKind::ControlKeyword) && has(py[3], "None", CodeTokenKind::Constant));
    auto lua = kinds(CodeLanguage::Lua, { "local function f(t) --[[ long", "comment ]] return t:len() end", "print([==[raw]==])" });
    CHECK(has(lua[0], "local", CodeTokenKind::Keyword) && has(lua[0], "f", CodeTokenKind::Function) && has(lua[1], "comment ]]", CodeTokenKind::Comment) &&
          has(lua[1], "end", CodeTokenKind::ControlKeyword) && has(lua[1], "len", CodeTokenKind::Function));
    CHECK(has(lua[2], "[==[raw]==]", CodeTokenKind::String) && has(lua[2], "print", CodeTokenKind::BuiltinType));
    auto java = kinds(CodeLanguage::Java, { "@Override public static void main(String[] args) { System.out.println('c'); }" });
    CHECK(has(java[0], "@Override", CodeTokenKind::Attribute) && has(java[0], "public", CodeTokenKind::Keyword) && has(java[0], "println", CodeTokenKind::Function) &&
          has(java[0], "String", CodeTokenKind::BuiltinType));
    auto cs = kinds(CodeLanguage::CSharp, { "#region Setup", "var p = @\"C:\\dir\"; string s = $\"{p}\"; foreach (var x in xs) {}" });
    CHECK(has(cs[0], "#region Setup", CodeTokenKind::Preprocessor) && has(cs[1], "@\"C:\\dir\"", CodeTokenKind::String) && has(cs[1], "foreach", CodeTokenKind::ControlKeyword));
    auto as = kinds(CodeLanguage::Assembly, { "section .text", "global _start", "_start: mov rax, 60 ; exit", "msg db \"hi\", 10", "    movq %rsp, %rbp  # gas", "    jmp .L2" });
    CHECK(has(as[0], "section", CodeTokenKind::Preprocessor) && has(as[1], "global", CodeTokenKind::Preprocessor) && has(as[2], "_start:", CodeTokenKind::Function));
    CHECK(has(as[2], "mov", CodeTokenKind::Keyword) && has(as[2], "rax", CodeTokenKind::Variable) && has(as[2], "60", CodeTokenKind::Number) && has(as[2], "; exit", CodeTokenKind::Comment));
    CHECK(has(as[3], "msg", CodeTokenKind::Function) && has(as[3], "db", CodeTokenKind::Preprocessor));
    CHECK(has(as[4], "%rsp", CodeTokenKind::Variable) && has(as[4], "# gas", CodeTokenKind::Comment) && has(as[5], "jmp", CodeTokenKind::ControlKeyword));
    CHECK(grove::code_line_comment(CodeLanguage::Python, "") == "#" && grove::code_line_comment(CodeLanguage::Lua, "") == "--" &&
          grove::code_line_comment(CodeLanguage::Assembly, ".globl main\nmain:\n movq %rsp, %rbp\n") == "#" && grove::code_line_comment(CodeLanguage::Assembly, "section .text\n") == ";");

    grove::CodeBuffer buf;
    buf.set_text("a = 1\n    b = 2\n");
    buf.set_caret(grove::CodePos{ 0, 0 });
    buf.set_caret(grove::CodePos{ 1, 3 }, true);
    buf.toggle_comment("#");
    CHECK(buf.text() == "# a = 1\n#     b = 2\n");
    buf.toggle_comment("#");
    CHECK(buf.text() == "a = 1\n    b = 2\n");
    buf.set_text("def f(x):");
    buf.set_caret(grove::CodePos{ 0, 9 });
    buf.new_line(4, true, CodeLanguage::Python, true);
    CHECK(buf.text() == "def f(x):\n    ");
    buf.insert("return x");
    buf.new_line(4, true, CodeLanguage::Python, true);
    CHECK(buf.text() == "def f(x):\n    return x\n");
    buf.set_text("if x then");
    buf.set_caret(grove::CodePos{ 0, 9 });
    buf.new_line(2, true, CodeLanguage::Lua, true);
    CHECK(buf.text() == "if x then\n  \nend");
}

void test_web_highlighting() {
    using grove::CodeTokenKind;
    using grove::CodeLanguage;
    auto kinds = [](CodeLanguage lang, const std::vector<std::string>& lines) {
        grove::CodeLineState st;
        std::vector<std::vector<std::pair<std::string, CodeTokenKind>>> out;
        for (const std::string& l : lines) {
            std::vector<std::pair<std::string, CodeTokenKind>> row;
            for (const grove::CodeToken& t : grove::highlight_code_line(lang, l, st)) row.emplace_back(l.substr(t.begin, t.end - t.begin), t.kind);
            out.push_back(row);
        }
        return out;
    };
    auto has = [](const std::vector<std::pair<std::string, CodeTokenKind>>& row, const std::string& text, CodeTokenKind kind) {
        for (const auto& [t, k] : row) {
            if (t == text && k == kind) return true;
        }
        return false;
    };
    CHECK(grove::code_language_for_path("site/index.html") == CodeLanguage::Html);
    CHECK(grove::code_language_for_path("a.css") == CodeLanguage::Css);
    CHECK(grove::code_language_for_path("app.mjs") == CodeLanguage::JavaScript);
    CHECK(grove::code_language_for_path("package.json") == CodeLanguage::Json);
    auto js = kinds(CodeLanguage::JavaScript, { "const x = this.count + 1; // c", "if (a) return /ab+c/gi.test(s);", "let t = `a ${b} c", "d`; doc.get(1)" });
    CHECK(has(js[0], "const", CodeTokenKind::Keyword) && has(js[0], "this", CodeTokenKind::ThisKeyword) && has(js[0], "count", CodeTokenKind::Property) && has(js[0], "// c", CodeTokenKind::Comment));
    CHECK(has(js[1], "if", CodeTokenKind::ControlKeyword) && has(js[1], "/ab+c/gi", CodeTokenKind::String) && has(js[1], "test", CodeTokenKind::Function));
    CHECK(has(js[2], "b", CodeTokenKind::Plain) && has(js[2], "${", CodeTokenKind::Operator));
    CHECK(has(js[3], "d`", CodeTokenKind::String) && has(js[3], "get", CodeTokenKind::Function));
    auto html = kinds(CodeLanguage::Html, { "<div class=\"a\">&amp;", "<script>", "let x = 2; </script><p>", "<style>p { color: red; }</style>" });
    CHECK(has(html[0], "div", CodeTokenKind::Tag) && has(html[0], "class", CodeTokenKind::Attribute) && has(html[0], "&amp;", CodeTokenKind::Constant));
    CHECK(has(html[2], "let", CodeTokenKind::Keyword) && has(html[2], "2", CodeTokenKind::Number) && has(html[2], "p", CodeTokenKind::Tag));
    CHECK(has(html[3], "color", CodeTokenKind::Property) && has(html[3], "style", CodeTokenKind::Tag));
    auto css = kinds(CodeLanguage::Css, { ".card > a:hover, #top {", "  color: #ff0000 !important;", "  margin: 4px calc(1em + 2px);", "}", "@media (max-width: 600px) {", "  p { --gap: 3px; }", "}" });
    CHECK(has(css[0], ".card", CodeTokenKind::Attribute) && has(css[0], "a", CodeTokenKind::Tag) && has(css[0], ":hover", CodeTokenKind::Keyword) && has(css[0], "#top", CodeTokenKind::Attribute));
    CHECK(has(css[1], "color", CodeTokenKind::Property) && has(css[1], "#ff0000", CodeTokenKind::Constant) && has(css[1], "!important", CodeTokenKind::ControlKeyword));
    CHECK(has(css[2], "margin", CodeTokenKind::Property) && has(css[2], "4px", CodeTokenKind::Number) && has(css[2], "calc", CodeTokenKind::Function));
    CHECK(has(css[4], "@media", CodeTokenKind::ControlKeyword) && has(css[5], "p", CodeTokenKind::Tag) && has(css[5], "--gap", CodeTokenKind::Variable));
    auto json = kinds(CodeLanguage::Json, { "{ \"name\": \"x\", \"n\": 2, \"ok\": true }" });
    CHECK(has(json[0], "\"name\"", CodeTokenKind::Property) && has(json[0], "\"x\"", CodeTokenKind::String) && has(json[0], "true", CodeTokenKind::Constant));
}

int main() {
    test_markup();
    test_markup_errors();
    test_bark();
    test_bark_errors();
    test_math();
    test_scripts();
    test_api();
    test_body_is_window();
    test_viewport_units();
    test_inputs();
    test_input_errors();
    test_input_api();
    test_switch();
    test_highlight();
    test_example();
    test_inputs_example();
    test_events();
    test_timers();
    test_query();
    test_pseudo_styles();
    test_tree_editing();
    test_c_events();
    test_bundle();
    test_todo_example();
    test_windows();
    test_popups();
    test_windows_example();
    test_code_buffer();
    test_code_highlight();
    test_code_and_file_inputs();
    test_natives();
    test_images();
    test_code_highlight_languages();
    test_walnut_highlight_detail();
    test_input_character_rules();
    test_color_formats();
    test_multi_cursor();
    test_pages_and_modals();
    test_alignment_and_math();
    test_zoom_and_select();
    test_keymap();
    test_dump_options();
    test_menus();
    test_completion();
    test_terminal_basics();
    test_change_stamps_and_style_cache();
    test_terminal_scrolling();
    test_terminal_alternate_screen();
    test_terminal_colors();
    test_terminal_unicode();
    test_terminal_replies_and_modes();
    test_terminal_input_encoding();
    test_terminal_selection_and_resize();
    test_code_find();
    test_web_highlighting();
    test_more_languages();
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
