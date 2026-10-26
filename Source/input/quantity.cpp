#include "grove/input/quantity.hpp"
#include "grove/input/input.hpp"

#include <fizmo/../../Source/Chemistry/units.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace grove {

namespace {

namespace chem = fizmo::chemistry;

struct QuantityTag {
    const char* tag;
    const char* dimension;
    const char* unit;
};

constexpr QuantityTag k_quantity_tags[] = {
    { "input_quantity", "", "" },
    { "input_temperature", "temperature", "K" },
    { "input_pressure", "pressure", "atm" },
    { "input_mass", "mass", "g" },
    { "input_length", "length", "m" },
    { "input_distance", "length", "m" },
    { "input_time", "time", "s" },
    { "input_volume", "volume", "L" },
    { "input_energy", "energy", "J" },
    { "input_amount", "amount", "mol" },
    { "input_density", "density", "g/cm\xC2\xB3" },
    { "input_concentration", "concentration", "M" },
};

const QuantityTag* tag_info(std::string_view tag) noexcept {
    for (const QuantityTag& t : k_quantity_tags) {
        if (tag == t.tag) return &t;
    }
    return nullptr;
}

chem::Dimension dimension_of(const Node& node) {
    return chem::dimension_named(quantity_dimension(node));
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::optional<double> number_of(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::string show(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[64];
    const double a = std::fabs(v);
    if (a != 0.0 && (a < 1e-4 || a >= 1e9)) std::snprintf(buf, sizeof buf, "%.6g", v);
    else std::snprintf(buf, sizeof buf, "%.10g", v);
    std::string s(buf);
    if (s.find('e') == std::string::npos && s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

std::string short_show(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    std::string s(buf);
    if (s == "-0") s = "0";
    return s;
}

Node* child_with_class(Node& node, std::string_view cls) {
    for (const auto& c : node.children()) {
        if (c->is_element() && c->has_class(cls)) return c.get();
    }
    return nullptr;
}

double to_si(double v, const std::string& unit, chem::Dimension d) {
    const chem::UnitInfo* u = chem::find_unit(unit, d);
    return u ? u->to_si(v) : v;
}

double from_si(double v, const std::string& unit, chem::Dimension d) {
    const chem::UnitInfo* u = chem::find_unit(unit, d);
    return u ? u->from_si(v) : v;
}

bool log_scale(const Node& node) {
    return node.has_attribute("log") && node.attribute_or("log", "") != "false";
}

bool has_slider(const Node& node) {
    return node.has_attribute("slider") && node.attribute_or("slider", "") != "false";
}

std::optional<double> stored_si(const Node& node, std::string_view name) {
    return number_of(node.attribute_or(name, ""));
}

void sync_slider(Document& document, Node& quantity) {
    Node* slider = child_with_class(quantity, "quantity-slider");
    if (!slider) return;
    const chem::Dimension d = dimension_of(quantity);
    const std::string unit = quantity_unit(quantity);
    const std::optional<double> lo_si = stored_si(quantity, "min-si");
    const std::optional<double> hi_si = stored_si(quantity, "max-si");
    double lo = lo_si ? from_si(*lo_si, unit, d) : 0.0;
    double hi = hi_si ? from_si(*hi_si, unit, d) : 100.0;
    if (hi < lo) std::swap(lo, hi);
    double v = quantity_value(quantity).value_or(lo);
    const bool lg = log_scale(quantity) && lo > 0.0;
    if (lg) {
        lo = std::log10(lo);
        hi = std::log10(hi);
        v = v > 0.0 ? std::log10(v) : lo;
    }
    v = std::min(hi, std::max(lo, v));
    double step = (hi - lo) / 400.0;
    if (!lg && quantity.attribute_or("range-unit", unit) == unit) {
        if (const std::optional<double> s = number_of(quantity.attribute_or("step", ""))) step = *s;
    }
    if (!(step > 0.0)) step = 1.0;
    slider->set_attribute("min", short_show(lo));
    slider->set_attribute("max", short_show(hi));
    slider->set_attribute("step", short_show(step));
    slider->set_attribute("value", show(v));
    document.touch();
}

void sync_children(Document& document, Node& quantity) {
    if (Node* number = child_with_class(quantity, "quantity-number")) {
        number->set_attribute("value", show(quantity_value(quantity).value_or(0.0)));
    }
    if (Node* select = child_with_class(quantity, "quantity-unit")) {
        select->set_attribute("value", quantity_unit(quantity));
    }
    sync_slider(document, quantity);
    document.touch();
}

void set_display(Document& document, Node& quantity, double value) {
    quantity.set_attribute("value", show(value));
    sync_children(document, quantity);
}

} // namespace

bool is_quantity_tag(std::string_view tag) noexcept {
    return tag_info(tag) != nullptr;
}

bool is_quantity(const Node& node) noexcept {
    return node.is_element() && is_quantity_tag(node.tag());
}

Node* quantity_of(Node& node) noexcept {
    if (is_quantity(node)) return &node;
    Node* p = node.parent();
    return p && is_quantity(*p) ? p : nullptr;
}

const Node* quantity_of(const Node& node) noexcept {
    if (is_quantity(node)) return &node;
    const Node* p = node.parent();
    return p && is_quantity(*p) ? p : nullptr;
}

std::string quantity_dimension(const Node& node) {
    if (node.has_attribute("dimension")) return node.attribute_or("dimension", "");
    const QuantityTag* t = tag_info(node.tag());
    if (t && *t->dimension) return t->dimension;
    if (node.has_attribute("unit")) {
        if (const chem::UnitInfo* u = chem::find_unit(node.attribute_or("unit", ""))) return chem::to_string(u->dimension);
    }
    return "dimensionless";
}

std::string quantity_unit(const Node& node) {
    const chem::Dimension d = dimension_of(node);
    const std::string u = node.attribute_or("unit", "");
    if (!u.empty()) {
        if (const chem::UnitInfo* info = chem::find_unit(u, d)) return info->symbol;
        return u;
    }
    const QuantityTag* t = tag_info(node.tag());
    if (t && *t->unit) return t->unit;
    return chem::si_unit(d).symbol;
}

std::vector<std::string> quantity_units(const Node& node) {
    const chem::Dimension d = dimension_of(node);
    std::vector<std::string> all = chem::unit_symbols(d);
    const std::vector<std::string> wanted = words(node.attribute_or("units", ""));
    if (wanted.empty()) return all;
    std::vector<std::string> out;
    for (const std::string& w : wanted) {
        if (const chem::UnitInfo* u = chem::find_unit(w, d)) {
            if (std::find(out.begin(), out.end(), u->symbol) == out.end()) out.push_back(u->symbol);
        }
    }
    const std::string current = quantity_unit(node);
    if (std::find(out.begin(), out.end(), current) == out.end()) out.insert(out.begin(), current);
    return out;
}

std::optional<double> quantity_value(const Node& node) {
    if (!is_quantity(node)) return std::nullopt;
    if (const std::optional<double> v = number_of(node.attribute_or("value", ""))) return v;
    return 0.0;
}

std::optional<double> quantity_si(const Node& node) {
    const std::optional<double> v = quantity_value(node);
    if (!v) return std::nullopt;
    return to_si(*v, quantity_unit(node), dimension_of(node));
}

std::string quantity_text(const Node& node) {
    if (!is_quantity(node)) return std::string();
    const std::string unit = quantity_unit(node);
    std::string out = show(quantity_value(node).value_or(0.0));
    if (!unit.empty()) out += " " + unit;
    return out;
}

bool set_quantity_si(Document& document, Node& node, double value_si) {
    if (!is_quantity(node) || !std::isfinite(value_si)) return false;
    set_display(document, node, from_si(value_si, quantity_unit(node), dimension_of(node)));
    return true;
}

bool set_quantity_value(Document& document, Node& node, double value, std::string_view unit) {
    if (!is_quantity(node) || !std::isfinite(value)) return false;
    if (unit.empty()) {
        set_display(document, node, value);
        return true;
    }
    const chem::Dimension d = dimension_of(node);
    if (!chem::find_unit(unit, d)) return false;
    return set_quantity_si(document, node, to_si(value, std::string(unit), d));
}

bool set_quantity_text(Document& document, Node& node, std::string_view text) {
    if (!is_quantity(node)) return false;
    const chem::Dimension d = dimension_of(node);
    std::string t(text);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || !std::isfinite(v)) return false;
    std::string rest(end);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    while (!rest.empty() && rest.back() == ' ') rest.pop_back();
    if (rest.empty()) return set_quantity_value(document, node, v);
    const chem::UnitInfo* u = chem::find_unit(rest, d);
    if (!u) return false;
    node.set_attribute("unit", u->symbol);
    set_display(document, node, v);
    return true;
}

bool set_quantity_unit(Document& document, Node& node, std::string_view unit) {
    if (!is_quantity(node)) return false;
    const chem::Dimension d = dimension_of(node);
    const chem::UnitInfo* u = chem::find_unit(unit, d);
    if (!u) return false;
    const double si = quantity_si(node).value_or(0.0);
    node.set_attribute("unit", u->symbol);
    set_display(document, node, u->from_si(si));
    return true;
}

bool expand_quantity(Document& document, Node& node) {
    if (!is_quantity(node) || node.has_attribute("data-expanded")) return false;
    const chem::Dimension d = dimension_of(node);
    const std::string unit = quantity_unit(node);
    node.set_attribute("unit", unit);
    const std::string range_unit = node.attribute_or("range-unit", unit);
    if (!node.has_attribute("value")) node.set_attribute("value", "0");
    if (const std::optional<double> lo = number_of(node.attribute_or("min", ""))) node.set_attribute("min-si", show(to_si(*lo, range_unit, d)));
    if (const std::optional<double> hi = number_of(node.attribute_or("max", ""))) node.set_attribute("max-si", show(to_si(*hi, range_unit, d)));
    if (has_slider(node) && !node.has_attribute("min-si")) {
        const double v = to_si(quantity_value(node).value_or(0.0), unit, d);
        node.set_attribute("min-si", show(log_scale(node) ? v / 1000.0 : std::min(0.0, v)));
    }
    if (has_slider(node) && !node.has_attribute("max-si")) {
        const double v = to_si(quantity_value(node).value_or(0.0), unit, d);
        node.set_attribute("max-si", show(log_scale(node) ? std::max(v, 1e-12) * 1000.0 : std::max(1.0, v * 4.0)));
    }
    const std::string id = node.id();
    Node* number = document.create_element("input_float");
    number->set_attribute("class", "quantity-number");
    if (!id.empty()) number->set_attribute("id", id + "-number");
    if (node.has_attribute("disabled")) number->set_attribute("disabled", "");
    document.append_child(node, *number);
    Node* select = document.create_element("input_select");
    select->set_attribute("class", "quantity-unit");
    if (!id.empty()) select->set_attribute("id", id + "-unit");
    if (node.has_attribute("disabled")) select->set_attribute("disabled", "");
    for (const std::string& u : quantity_units(node)) {
        Node* option = document.create_element("option");
        option->set_attribute("value", u);
        option->set_inner_text(u);
        document.append_child(*select, *option);
    }
    document.append_child(node, *select);
    if (has_slider(node)) {
        Node* slider = document.create_element("input_slider");
        slider->set_attribute("class", "quantity-slider");
        if (!id.empty()) slider->set_attribute("id", id + "-slider");
        if (node.has_attribute("disabled")) slider->set_attribute("disabled", "");
        document.append_child(node, *slider);
    }
    node.set_attribute("data-expanded", "true");
    sync_children(document, node);
    return true;
}

std::size_t expand_quantity_inputs(Document& document) {
    std::vector<Node*> pending;
    document.root().walk([&](Node& n) {
        if (is_quantity(n) && !n.has_attribute("data-expanded")) pending.push_back(&n);
    });
    std::size_t done = 0;
    for (Node* n : pending) done += expand_quantity(document, *n) ? 1 : 0;
    return done;
}

bool quantity_event(Document& document, Node& target, const Event& event) {
    if (event.type != "input" && event.type != "change") return false;
    Node* quantity = target.parent();
    if (!quantity || !is_quantity(*quantity)) return false;
    const chem::Dimension d = dimension_of(*quantity);
    if (target.has_class("quantity-number")) {
        const std::optional<double> v = input_float(target);
        if (!v) return false;
        quantity->set_attribute("value", show(*v));
        sync_slider(document, *quantity);
        return true;
    }
    if (target.has_class("quantity-unit")) {
        const std::optional<std::string> u = input_string(target);
        if (!u || *u == quantity_unit(*quantity)) return false;
        const chem::UnitInfo* info = chem::find_unit(*u, d);
        if (!info) return false;
        const double si = quantity_si(*quantity).value_or(0.0);
        quantity->set_attribute("unit", info->symbol);
        set_display(document, *quantity, info->from_si(si));
        return true;
    }
    if (target.has_class("quantity-slider")) {
        const std::optional<double> s = input_float(target);
        if (!s) return false;
        const std::string unit = quantity_unit(*quantity);
        const std::optional<double> lo_si = stored_si(*quantity, "min-si");
        const bool lg = log_scale(*quantity) && lo_si && from_si(*lo_si, unit, d) > 0.0;
        const double v = lg ? std::pow(10.0, *s) : *s;
        quantity->set_attribute("value", show(v));
        if (Node* number = child_with_class(*quantity, "quantity-number")) number->set_attribute("value", show(v));
        document.touch();
        return true;
    }
    return false;
}

std::vector<std::string> quantity_problems(const Node& node) {
    std::vector<std::string> out;
    if (!is_quantity(node)) return out;
    const std::string dim = quantity_dimension(node);
    const chem::Dimension d = chem::dimension_named(dim);
    if (dim != "dimensionless" && d == chem::Dimension::Dimensionless) out.push_back("has dimension=\"" + dim + "\", which isn't one Grove knows (try temperature, pressure, mass, length, time, volume, energy...)");
    const std::string unit = node.attribute_or("unit", "");
    if (!unit.empty() && !chem::find_unit(unit, d)) out.push_back("has unit=\"" + unit + "\", which isn't a unit of " + dim);
    for (const std::string& w : words(node.attribute_or("units", ""))) {
        if (!chem::find_unit(w, d)) out.push_back("lists \"" + w + "\" in units=, which isn't a unit of " + dim);
    }
    if (node.has_attribute("value") && !number_of(node.attribute_or("value", ""))) out.push_back("has a value that isn't a number");
    if (log_scale(node) && has_slider(node)) {
        const std::optional<double> lo = number_of(node.attribute_or("min", ""));
        if (lo && *lo <= 0.0) out.push_back("has a log slider, so min has to be above 0");
    }
    return out;
}

} // namespace grove
