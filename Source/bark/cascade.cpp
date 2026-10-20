#include "grove/bark/cascade.hpp"
#include "grove/pages.hpp"
#include "grove/bark/parser.hpp"
#include "grove/bark/properties.hpp"

#include <cctype>
#include <string_view>

#include <algorithm>

namespace grove {
namespace bark {

namespace {

constexpr const char* k_inherited[] = {
    "code-keyword",
    "code-type",
    "code-function",
    "code-namespace",
    "code-variable",
    "code-constant",
    "code-string",
    "code-escape",
    "code-number",
    "code-comment",
    "code-preprocessor",
    "code-operator",
    "code-tag",
    "code-attribute",
    "code-property",
    "code-mark",
    "code-plain",
    "code-builtin-operator",
    "code-builtin-type",
    "code-cast",
    "code-control-keyword",
    "code-declaration",
    "code-enum-member",
    "code-error-line",
    "code-error-underline",
    "code-overloaded-operator",
    "code-self",
    "code-template-scope",
    "code-this",
    "code-unresolved",
    "code-variable-template",
    "code-warning-underline",
    "code-hover",
    "color",
    "font",
    "font-family",
    "font-size",
    "font-weight",
    "font-style",
    "line-height",
    "text-align",
    "letter-spacing",
    "white-space",
    "math-syntax",
    "math-color",
    "accent-color",
    "caret-color",
    "cursor",
    "user-select",
    "selection-color",
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

void split_align(const ValueList& align, std::string& horizontal, std::string& vertical) {
    horizontal.clear();
    vertical.clear();
    std::size_t centers = 0;
    for (const Value& w : align) {
        if (!w.is_ident() && !w.is_string()) continue;
        const std::string& t = w.text;
        if (t == "top" || t == "bottom") vertical = t;
        else if (t == "middle") vertical = "center";
        else if (t == "left" || t == "right" || t == "start" || t == "end") horizontal = t == "start" ? "left" : t == "end" ? "right" : t;
        else if (t == "center") ++centers;
    }
    if (centers > 0 && align.size() == 1) {
        horizontal = vertical = "center";
        return;
    }
    for (std::size_t i = 0; i < centers; ++i) {
        if (vertical.empty()) vertical = "center";
        else if (horizontal.empty()) horizontal = "center";
    }
}

namespace {

std::uint64_t name_hash(std::string_view name) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    for (const unsigned char c : name) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h | 1u;
}

} // namespace

void ComputedStyle::build_index() const {
    std::size_t size = 8;
    while (size < m_values.size() * 2 + 2) size *= 2;
    m_index.assign(size, Slot{});
    for (const auto& [name, values] : m_values) {
        const std::uint64_t h = name_hash(name);
        std::size_t i = static_cast<std::size_t>(h) & (size - 1);
        while (m_index[i].hash != 0) i = (i + 1) & (size - 1);
        m_index[i] = Slot{ h, &name, &values };
    }
}

const ValueList* ComputedStyle::get_view(std::string_view property) const {
    if (m_values.empty()) return nullptr;
    if (m_index.empty()) build_index();
    const std::uint64_t h = name_hash(property);
    const std::size_t mask = m_index.size() - 1;
    for (std::size_t i = static_cast<std::size_t>(h) & mask;; i = (i + 1) & mask) {
        const Slot& slot = m_index[i];
        if (slot.hash == 0) return nullptr;
        if (slot.hash == h && *slot.name == property) return slot.values;
    }
}

std::optional<Rgba> ComputedStyle::color(const std::string& property) const {
    const ValueList* v = get(property);
    return v ? as_color(*v) : std::nullopt;
}

std::optional<double> ComputedStyle::length(const std::string& property, std::size_t index) const {
    const ValueList* v = get(property);
    return v ? as_length(*v, font_size(), m_viewport, index) : std::nullopt;
}

std::string_view ComputedStyle::text_view(std::string_view property) const {
    const ValueList* v = get_view(property);
    if (!v || v->empty()) return std::string_view();
    if (v->size() == 1 && (v->front().is_ident() || v->front().is_string())) return v->front().text;
    static thread_local std::string joined;
    joined = as_text(*v).value_or("");
    return joined;
}

std::optional<std::string> ComputedStyle::text(const std::string& property) const {
    const ValueList* v = get(property);
    return v ? as_text(*v) : std::nullopt;
}

double ComputedStyle::font_size() const {
    const ValueList* v = get("font-size");
    if (!v) return 16.0 * m_viewport.scale();
    const auto px = as_length(*v, 16.0 * m_viewport.scale(), m_viewport);
    return px ? *px : 16.0 * m_viewport.scale();
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
            "u { text-decoration: underline; }\n"
            "code { font-family: monospace; }\n"
            "pre { white-space: pre; font-family: monospace; margin: 0 0 0.5em 0; }\n"
            "link { color: #89b4fa; cursor: pointer; }\n"
            "center { align: center; flex: 1; }\n"
            "modal {\n"
            "    width: 480px; padding: 20px 24px; border-radius: 10px; background: #24273a; color: #cdd6f4;\n"
            "    border-color: rgba(255, 255, 255, 18%); backdrop: rgba(0, 0, 0, 55%);\n"
            "}\n"
            "p { margin: 0 0 0.5em 0; }\n"
            "math { display: inline; }\n"
            "input, input_integer, input_float, input_string, input_color, input_select {\n"
            "    padding: 6px 10px; margin: 0 0 8px 0;\n"
            "    background: rgba(255, 255, 255, 6%); border-color: rgba(255, 255, 255, 22%); border-radius: 6px;\n"
            "}\n"
            "input, input_integer, input_float, input_string, input_color { width: 220px; }\n"
            "input_slider, slider { width: 220px; margin: 0 0 8px 0; }\n"
            "input_select { cursor: pointer; }\n"
            "button, input_file, input_bool, input_radio, input_select { user-select: none; }\n"
            "option { display: none; }\n"
            "input_code {\n"
            "    height: 300px; margin: 0 0 8px 0; font-family: monospace; font-size: 14px; cursor: text;\n"
            "    background: rgba(0, 0, 0, 25%); border-color: rgba(255, 255, 255, 15%); border-radius: 6px;\n"
            "}\n"
            "input_file {\n"
            "    padding: 6px 14px; margin: 0 0 8px 0; cursor: pointer; border-radius: 6px;\n"
            "    background: rgba(255, 255, 255, 10%); border-color: rgba(255, 255, 255, 25%);\n"
            "}\n"
            "input_file:hover { background: rgba(255, 255, 255, 16%); }\n"
            "input_file:active { background: rgba(255, 255, 255, 24%); }\n"
            "input_file:disabled { opacity: 0.45; cursor: default; }\n"
            "input_bool, input_radio, checkbox { margin: 0 0 6px 0; appearance: checkbox; cursor: pointer; }\n"
            "input_slider, slider { cursor: pointer; }\n"
            "button {\n"
            "    padding: 6px 14px; margin: 0 0 8px 0; cursor: pointer; border-radius: 6px;\n"
            "    background: rgba(255, 255, 255, 10%); border-color: rgba(255, 255, 255, 25%);\n"
            "}\n"
            "button:hover { background: rgba(255, 255, 255, 16%); }\n"
            "button:active { background: rgba(255, 255, 255, 24%); }\n"
            "button:focus { border-color: rgba(255, 255, 255, 45%); }\n"
            "button:disabled { opacity: 0.45; cursor: default; }\n"
            "row { display: row; gap: 8px; }\n"
            "column { display: column; }\n"
            "body { accent-color: #89b4fa; }\n"
            "window.popup { background: #1e1e2e; color: #cdd6f4; padding: 20px 22px 16px 22px; accent-color: #89b4fa; font-size: 15px; }\n"
            ".popup-main { gap: 16px; }\n"
            ".popup-icon {\n"
            "    width: 36px; height: 36px; border-radius: 18px; margin: 0; padding: 5px 0 0 0;\n"
            "    text-align: center; font-size: 20px; font-weight: bold; color: #1e1e2e; background: #89b4fa;\n"
            "}\n"
            ".popup-warning .popup-icon { background: #f9e2af; }\n"
            ".popup-error .popup-icon { background: #f38ba8; }\n"
            ".popup-question .popup-icon { background: #a6e3a1; }\n"
            ".popup-text { padding: 7px 0 0 0; }\n"
            ".popup-message { margin: 0 0 6px 0; }\n"
            ".popup-input { width: 300px; margin: 8px 0 0 0; }\n"
            ".popup-choice { margin: 0 0 6px 0; }\n"
            ".popup-buttons { justify-content: end; margin: 18px 0 0 0; }\n"
            ".popup-buttons button { margin: 0; padding: 6px 18px; }\n"
            ".popup-buttons .popup-ok { background: #89b4fa; color: #1e1e2e; border-color: #89b4fa; }\n"
            ".popup-buttons .popup-ok:hover { background: #b4cefb; }\n"
            ".popup-buttons .popup-ok:focus { border-color: #ffffff; }\n",
            "<default>", ignored);
    }();
    return sheet;
}

void StyleEngine::set_viewport(const Viewport& viewport) {
    if (viewport == m_viewport) return;
    m_viewport = viewport;
    m_cache.clear();
    m_recent = RecentSlot();
    ++m_revision;
}

void StyleEngine::add(const Stylesheet& sheet) {
    m_sheet.append(sheet);
    m_cache.clear();
    m_recent = RecentSlot();
    m_index_ready = false;
    ++m_revision;
}

std::uint64_t StyleEngine::style_key(const Node& node, Diagnostics& diagnostics) {
    style_of(node, diagnostics);
    return m_recent.node == &node ? m_recent.entry->key : 0;
}

void StyleEngine::prune() {
    m_recent = RecentSlot();
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->second.checked + 2 < m_epoch) it = m_cache.erase(it);
        else ++it;
    }
    m_prune_at = std::max<std::size_t>(4096, m_cache.size() * 2);
}

void StyleEngine::build_index() const {
    m_index = RuleIndex{};
    std::size_t order = 0;
    const std::vector<Rule>& rules = m_sheet.rules();
    m_index.first_order.reserve(rules.size());
    for (std::size_t r = 0; r < rules.size(); ++r) {
        m_index.first_order.push_back(order);
        order += rules[r].declarations.size();
        for (const Selector& s : rules[r].selectors) {
            for (const Compound& c : s.parts) {
                for (const std::string& p : c.pseudos) {
                    if (p == "first-child" || p == "last-child" || p == "only-child") m_index.uses_position = true;
                    else if (p == "empty") m_index.uses_empty = true;
                    else if (p == "current") m_index.uses_current = true;
                }
            }
            if (s.parts.empty()) continue;
            const Compound& last = s.parts.back();
            std::vector<std::size_t>* bucket = nullptr;
            if (!last.id.empty()) bucket = &m_index.by_id[last.id];
            else if (!last.classes.empty()) bucket = &m_index.by_class[last.classes.front()];
            else if (!last.tag.empty() && last.tag != "*") bucket = &m_index.by_tag[last.tag];
            else bucket = &m_index.universal;
            if (bucket->empty() || bucket->back() != r) bucket->push_back(r);
        }
    }
    m_index_ready = true;
}

namespace {

[[maybe_unused]] void mix(std::uint64_t& h, std::string_view text) {
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    h ^= 0xff;
    h *= 1099511628211ull;
}

void mix(std::uint64_t& h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xff;
        h *= 1099511628211ull;
    }
}

std::size_t element_position(const Node& node, bool from_end) {
    const Node* parent = node.parent();
    if (!parent) return 0;
    std::size_t before = 0;
    std::size_t after = 0;
    bool seen = false;
    for (const auto& c : parent->children()) {
        if (!c->is_element()) continue;
        if (c.get() == &node) seen = true;
        else if (seen) ++after;
        else ++before;
    }
    return from_end ? after : before;
}

} // namespace

std::uint64_t StyleEngine::key_of(const Node& node, std::uint64_t parent_key) const {
    std::uint64_t h = 14695981039346656037ull;
    mix(h, parent_key);
    mix(h, node.self_stamp());
    if (!node.is_element()) return h;
    std::uint64_t flags = 1;
    if (node.tag() == "page" && is_page_current(node)) flags |= 2u;
    if (m_index.uses_empty && node.children().empty()) flags |= 4u;
    if (m_index.uses_current && is_navigation_current(node)) flags |= 8u;
    if (m_index.uses_position) {
        if (element_position(node, false) == 0) flags |= 16u;
        if (element_position(node, true) == 0) flags |= 32u;
    }
    mix(h, flags);
    return h;
}

ComputedStyle StyleEngine::compute(const Node& node, const ComputedStyle* parent, Diagnostics& diagnostics) const {
    (void)diagnostics;
    ComputedStyle out;
    out.set_viewport(m_viewport);
    if (parent) {
        for (const auto& [name, values] : parent->values()) {
            if (is_inherited_property(name) || is_custom_property(name)) out.set(name, values);
        }
    }
    if (!node.is_element()) return out;
    if (node.has_attribute("center")) {
        out.set("align", ValueList{ Value::make_ident("center") });
    } else if (const Attribute* a = node.find_attribute("align")) {
        ValueList words;
        std::string word;
        for (char c : a->value + " ") {
            if (c == ' ' || c == ',' || c == '\t') {
                if (!word.empty()) words.push_back(Value::make_ident(word));
                word.clear();
            } else {
                word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        if (!words.empty()) out.set("align", words);
    }

    if (!m_index_ready) build_index();
    std::vector<std::size_t> picks = m_index.universal;
    auto take = [&picks](const std::unordered_map<std::string, std::vector<std::size_t>>& map, const std::string& key) {
        const auto found = map.find(key);
        if (found != map.end()) picks.insert(picks.end(), found->second.begin(), found->second.end());
    };
    take(m_index.by_tag, node.tag());
    if (const Attribute* id = node.find_attribute("id")) take(m_index.by_id, id->value);
    if (const Attribute* cls = node.find_attribute("class")) {
        const std::string& list = cls->value;
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && std::isspace(static_cast<unsigned char>(list[i]))) ++i;
            const std::size_t start = i;
            while (i < list.size() && !std::isspace(static_cast<unsigned char>(list[i]))) ++i;
            if (i > start) take(m_index.by_class, list.substr(start, i - start));
        }
    }
    std::sort(picks.begin(), picks.end());
    picks.erase(std::unique(picks.begin(), picks.end()), picks.end());

    std::vector<Candidate> candidates;
    const std::vector<Rule>& rules = m_sheet.rules();
    for (const std::size_t r : picks) {
        const Rule& rule = rules[r];
        Specificity best;
        bool matched = false;
        for (const Selector& s : rule.selectors) {
            if (!s.matches(node)) continue;
            const Specificity sp = s.specificity();
            if (!matched || best < sp) best = sp;
            matched = true;
        }
        if (!matched) continue;
        std::size_t order = m_index.first_order[r];
        for (const Declaration& d : rule.declarations) candidates.push_back(Candidate{ best, d.important, false, ++order, &d });
    }
    std::size_t order = m_index.first_order.empty() ? 0 : m_index.first_order.back() + rules.back().declarations.size();

    std::vector<Declaration> inline_decls;
    if (const Attribute* a = node.find_attribute("style")) {
        Diagnostics checked_at_load;
        inline_decls = parse_declarations(a->value, node.tag() + "[style]", checked_at_load, m_sheet.variables());
        for (const Declaration& d : inline_decls) candidates.push_back(Candidate{ Specificity{}, d.important, true, ++order, &d });
    }

    std::stable_sort(candidates.begin(), candidates.end(), weaker);
    bool declared_text_align = false;
    bool has_vars = false;
    for (const Candidate& c : candidates) {
        const Declaration& d = *c.declaration;
        if (d.property == "text-align") declared_text_align = true;
        if (d.values.size() == 1 && d.values.front().is_ident("inherit")) {
            const ValueList* pv = parent ? parent->get(d.property) : nullptr;
            if (pv) out.set(d.property, *pv);
            continue;
        }
        out.set(d.property, d.values);
        if (!has_vars && uses_var(d.values)) has_vars = true;
    }
    if (has_vars) {
        auto lookup = [&out](const std::string& name) { return out.get(name); };
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<std::pair<std::string, ValueList>> resolved;
            for (const auto& [name, values] : out.values()) {
                if (!uses_var(values)) continue;
                if (pass == 0 && !is_custom_property(name)) continue;
                ValueList copy = values;
                substitute_vars(copy, lookup);
                resolved.emplace_back(name, std::move(copy));
            }
            for (auto& [name, values] : resolved) out.set(name, std::move(values));
        }
    }

    if (const ValueList* ta = out.get("text-align"); ta && ta->size() == 1 && ta->front().is_ident()) {
        const std::string& t = ta->front().text;
        if (t == "start") out.set("text-align", ValueList{ Value::make_ident("left") });
        else if (t == "end") out.set("text-align", ValueList{ Value::make_ident("right") });
        else if (t == "middle") out.set("text-align", ValueList{ Value::make_ident("center") });
    }
    if (!declared_text_align) {
        if (const ValueList* align = out.get("align")) {
            std::string horizontal;
            std::string vertical;
            split_align(*align, horizontal, vertical);
            if (!horizontal.empty()) out.set("text-align", ValueList{ Value::make_ident(horizontal) });
        }
    }

    if (node.is_element() && ((node.tag() == "page" && !is_page_current(node)) || node.tag() == "modal")) {
        out.set("display", ValueList{ Value::make_ident("none") });
    }

    if (const ValueList* fs = out.get("font-size")) {
        if (!fs->empty() && fs->front().is_number()) {
            const Value& v = fs->front();
            const double scale = m_viewport.scale();
            const double base = parent ? parent->font_size() : 16.0 * scale;
            if (v.unit == "em" || v.unit == "%") {
                const double factor = v.unit == "%" ? v.number / 100.0 : v.number;
                out.set("font-size", ValueList{ Value::make_number(base * factor / scale, "px") });
            } else if (const auto per = viewport_unit_size(v.unit, m_viewport)) {
                out.set("font-size", ValueList{ Value::make_number(v.number * *per / scale, "px") });
            }
        }
    }
    return out;
}

const ComputedStyle& StyleEngine::style_of(const Node& node, Diagnostics& diagnostics) {
    if (m_recent.node == &node && m_recent.entry->checked == m_epoch) return m_recent.entry->style;
    auto it = m_cache.find(&node);
    if (it != m_cache.end() && it->second.checked == m_epoch) {
        m_recent.node = &node;
        m_recent.entry = &it->second;
        return it->second.style;
    }
    if (!m_index_ready) build_index();
    const ComputedStyle* parent = nullptr;
    std::uint64_t parent_key = 0;
    if (const Node* p = node.parent()) {
        parent = &style_of(*p, diagnostics);
        const auto pe = m_cache.find(p);
        if (pe != m_cache.end()) parent_key = pe->second.key;
    }
    const std::uint64_t key = key_of(node, parent_key);
    it = m_cache.find(&node);
    if (it != m_cache.end() && it->second.key == key) {
        it->second.checked = m_epoch;
        m_recent.node = &node;
        m_recent.entry = &it->second;
        return it->second.style;
    }
    ComputedStyle computed = compute(node, parent, diagnostics);
    Entry& slot = it != m_cache.end() ? it->second : m_cache[&node];
    slot.key = key;
    slot.checked = m_epoch;
    slot.style = std::move(computed);
    m_recent.node = &node;
    m_recent.entry = &slot;
    return slot.style;
}

} // namespace bark
} // namespace grove
