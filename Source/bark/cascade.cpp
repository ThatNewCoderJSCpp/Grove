#include "grove/bark/cascade.hpp"
#include "grove/bark/parser.hpp"

#include <algorithm>

namespace grove {
namespace bark {

namespace {

constexpr const char* k_inherited[] = {
    "color",
    "font",
    "font-family",
    "font-size",
    "font-weight",
    "font-style",
    "line-height",
    "text-align",
    "letter-spacing",
    "math-syntax",
    "math-color",
    "accent-color",
    "cursor",
};

struct Candidate {
    Specificity specificity;
    bool important = false;
    bool inline_style = false;
    std::size_t order = 0;
    const Declaration* declaration = nullptr;
};

bool weaker(const Candidate& a, const Candidate& b) {
    if (a.important != b.important) return !a.important;
    if (a.inline_style != b.inline_style) return !a.inline_style;
    if (!(a.specificity == b.specificity)) return a.specificity < b.specificity;
    return a.order < b.order;
}

} // namespace

bool is_inherited_property(const std::string& property) noexcept {
    for (const char* p : k_inherited) {
        if (property == p) return true;
    }
    return false;
}

const ValueList* ComputedStyle::get(const std::string& property) const {
    auto it = m_values.find(property);
    return it == m_values.end() ? nullptr : &it->second;
}

std::optional<Rgba> ComputedStyle::color(const std::string& property) const {
    const ValueList* v = get(property);
    return v ? as_color(*v) : std::nullopt;
}

std::optional<double> ComputedStyle::length(const std::string& property, std::size_t index) const {
    const ValueList* v = get(property);
    return v ? as_length(*v, font_size(), m_viewport, index) : std::nullopt;
}

std::optional<std::string> ComputedStyle::text(const std::string& property) const {
    const ValueList* v = get(property);
    return v ? as_text(*v) : std::nullopt;
}

double ComputedStyle::font_size() const {
    const ValueList* v = get("font-size");
    if (!v) return 16.0;
    const auto px = as_length(*v, 16.0, m_viewport);
    return px ? *px : 16.0;
}

const Stylesheet& default_stylesheet() {
    static const Stylesheet sheet = [] {
        Diagnostics ignored;
        return parse_stylesheet(
            "head, title, style, script { display: none; }\n"
            "body window { display: none; }\n"
            "h1 { font-size: 2em; font-weight: bold; }\n"
            "h2 { font-size: 1.5em; font-weight: bold; }\n"
            "h3 { font-size: 1.25em; font-weight: bold; }\n"
            "h4 { font-size: 1.1em; font-weight: bold; }\n"
            "b { font-weight: bold; }\n"
            "i { font-style: italic; }\n"
            "code { font-family: monospace; }\n"
            "p { margin: 0 0 0.5em 0; }\n"
            "math { display: inline; }\n"
            "input, input_integer, input_float, input_string, input_color {\n"
            "    width: 220px; padding: 6px 10px; margin: 0 0 8px 0;\n"
            "    background: rgba(255, 255, 255, 6%); border-color: rgba(255, 255, 255, 22%); border-radius: 6px;\n"
            "}\n"
            "input_slider, slider { width: 220px; margin: 0 0 8px 0; }\n"
            "input_bool, input_radio, checkbox { margin: 0 0 6px 0; appearance: checkbox; }\n"
            "body { accent-color: #89b4fa; }\n",
            "<default>", ignored);
    }();
    return sheet;
}

void StyleEngine::set_viewport(const Viewport& viewport) {
    if (viewport == m_viewport) return;
    m_viewport = viewport;
    m_cache.clear();
}

void StyleEngine::add(const Stylesheet& sheet) {
    m_sheet.append(sheet);
    m_cache.clear();
}

ComputedStyle StyleEngine::compute(const Node& node, const ComputedStyle* parent, Diagnostics& diagnostics) const {
    ComputedStyle out;
    out.set_viewport(m_viewport);
    if (parent) {
        for (const auto& [name, values] : parent->values()) {
            if (is_inherited_property(name)) out.set(name, values);
        }
    }
    if (!node.is_element()) return out;

    std::vector<Candidate> candidates;
    std::size_t order = 0;
    for (const Rule& rule : m_sheet.rules()) {
        Specificity best;
        bool matched = false;
        for (const Selector& s : rule.selectors) {
            if (!s.matches(node)) continue;
            const Specificity sp = s.specificity();
            if (!matched || best < sp) best = sp;
            matched = true;
        }
        for (const Declaration& d : rule.declarations) {
            ++order;
            if (matched) candidates.push_back(Candidate{ best, d.important, false, order, &d });
        }
    }

    std::vector<Declaration> inline_decls;
    if (const Attribute* a = node.find_attribute("style")) {
        inline_decls = parse_declarations(a->value, node.tag() + "[style]", diagnostics, m_sheet.variables());
        for (const Declaration& d : inline_decls) candidates.push_back(Candidate{ Specificity{}, d.important, true, ++order, &d });
    }

    std::stable_sort(candidates.begin(), candidates.end(), weaker);
    for (const Candidate& c : candidates) {
        const Declaration& d = *c.declaration;
        if (d.values.size() == 1 && d.values.front().is_ident("inherit")) {
            const ValueList* pv = parent ? parent->get(d.property) : nullptr;
            if (pv) out.set(d.property, *pv);
            continue;
        }
        out.set(d.property, d.values);
    }

    if (const ValueList* fs = out.get("font-size")) {
        if (!fs->empty() && fs->front().is_number()) {
            const Value& v = fs->front();
            const double base = parent ? parent->font_size() : 16.0;
            if (v.unit == "em" || v.unit == "%") {
                const double factor = v.unit == "%" ? v.number / 100.0 : v.number;
                out.set("font-size", ValueList{ Value::make_number(base * factor, "px") });
            } else if (const auto per = viewport_unit_size(v.unit, m_viewport)) {
                out.set("font-size", ValueList{ Value::make_number(v.number * *per, "px") });
            }
        }
    }
    return out;
}

const ComputedStyle& StyleEngine::style_of(const Node& node, Diagnostics& diagnostics) {
    auto it = m_cache.find(&node);
    if (it != m_cache.end()) return it->second;
    const ComputedStyle* parent = node.parent() ? &style_of(*node.parent(), diagnostics) : nullptr;
    ComputedStyle computed = compute(node, parent, diagnostics);
    return m_cache.emplace(&node, std::move(computed)).first->second;
}

} // namespace bark
} // namespace grove
