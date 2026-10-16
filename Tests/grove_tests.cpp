#include "grove/grove.hpp"
#include "grove/api/grove_api.h"

#include <iostream>
#include <limits>
#include <string>

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
    test_example();
    test_inputs_example();
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
