#include "grove/grove.hpp"
#include "grove/api/grove_api.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
