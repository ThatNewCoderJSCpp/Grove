#include "grove/grove.hpp"
#include "grove/api/grove_api.h"

#include <iostream>
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

void test_example() {
    grove::Project project = grove::load_project(std::string(GROVE_EXAMPLES_DIR) + "/hello/hello.grove");
    if (!project.diagnostics.ok()) project.diagnostics.print(std::cerr);
    CHECK(project.diagnostics.ok());
    CHECK(project.math.size() == 2);
    CHECK(project.scripts.size() == 2);
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
    test_example();
    std::cout << (g_checks - g_failures) << '/' << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
