#include "grove/input/element_table.hpp"

#include <fizmo/../../Source/Chemistry/nomenclature.hpp>
#include <fizmo/../../Source/Chemistry/periodic_table.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace grove {

namespace {

namespace chem = fizmo::chemistry;

std::vector<std::string> words_of(const std::string& text) {
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

std::vector<ElementCell> build_cells() {
    std::vector<ElementCell> out;
    for (const chem::Element& e : chem::PeriodicTable::elements()) {
        ElementCell c;
        c.number = e.atomic_number;
        c.group = e.group;
        c.period = e.period;
        c.block = chem::to_string(e.block)[0];
        c.mass = e.atomic_weight;
        c.symbol = e.symbol;
        c.name = e.name;
        c.category = chem::to_string(e.category);
        c.classification = chem::to_string(e.classification);
        if (e.group == 0) {
            c.row = e.period == 6 ? 8 : 9;
            c.column = 2 + (e.period == 6 ? e.atomic_number - 57 : e.atomic_number - 89);
        } else {
            c.row = e.period - 1;
            c.column = e.group - 1;
        }
        out.push_back(c);
    }
    return out;
}

bool token_matches(const std::string& want, const std::string& have) {
    if (want == have) return true;
    if (!want.empty() && want.back() == 's' && want.substr(0, want.size() - 1) == have) return true;
    if (want == "alkaline-earth" && have == "alkaline-earth-metal") return true;
    if (want == "alkali" && have == "alkali-metal") return true;
    if (want == "transition" && have == "transition-metal") return true;
    if (want == "nonmetal" && (have == "reactive-nonmetal" || have == "halogen" || have == "noble-gas")) return true;
    if (want == "inner-transition-metal" && (have == "lanthanide" || have == "actinide")) return true;
    return false;
}

bool list_has(const Node& node, const char* attribute, const std::string& value) {
    const Attribute* a = node.find_attribute(attribute);
    if (!a) return false;
    for (const std::string& w : words_of(a->value)) {
        if (token_matches(element_token(w), value)) return true;
    }
    return false;
}

bool list_has_number(const Node& node, const char* attribute, int value) {
    const Attribute* a = node.find_attribute(attribute);
    if (!a) return false;
    for (const std::string& w : words_of(a->value)) {
        const std::size_t dash = w.find('-');
        if (dash != std::string::npos && dash > 0) {
            const int lo = std::atoi(w.substr(0, dash).c_str());
            const int hi = std::atoi(w.substr(dash + 1).c_str());
            if (value >= lo && value <= hi) return true;
            continue;
        }
        if (w == "f" && value == 0) return true;
        if (std::atoi(w.c_str()) == value && (value != 0 || w == "0")) return true;
    }
    return false;
}

bool symbol_listed(const Node& node, const char* attribute, int number) {
    const Attribute* a = node.find_attribute(attribute);
    if (!a) return false;
    for (const std::string& w : words_of(a->value)) {
        if (element_number(w) == number) return true;
    }
    return false;
}

} // namespace

const std::vector<ElementCell>& element_cells() {
    static const std::vector<ElementCell> cells = build_cells();
    return cells;
}

const ElementCell* element_cell(int number) noexcept {
    const std::vector<ElementCell>& cells = element_cells();
    if (number < 1 || number > static_cast<int>(cells.size())) return nullptr;
    return &cells[static_cast<std::size_t>(number - 1)];
}

const ElementCell* element_cell_at(int row, int column) noexcept {
    for (const ElementCell& c : element_cells()) {
        if (c.row == row && c.column == column) return &c;
    }
    return nullptr;
}

int element_number(std::string_view text) noexcept {
    std::string t;
    for (char c : text) {
        if (c != ' ') t += c;
    }
    if (t.empty()) return 0;
    if (std::isalpha(static_cast<unsigned char>(t.front()))) {
        std::size_t cut = t.size();
        for (std::size_t i = 1; i < t.size(); ++i) {
            const char c = t[i];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '^') {
                cut = i;
                break;
            }
        }
        bool tail_ok = true;
        for (std::size_t i = cut; i < t.size(); ++i) tail_ok = tail_ok && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '+' || t[i] == '-' || t[i] == '^');
        if (tail_ok && cut < t.size()) t.resize(cut);
    }
    bool digits = true;
    for (char c : t) digits = digits && std::isdigit(static_cast<unsigned char>(c));
    if (digits) {
        const int n = std::atoi(t.c_str());
        return n >= 1 && n <= static_cast<int>(element_cells().size()) ? n : 0;
    }
    for (const ElementCell& c : element_cells()) {
        if (c.symbol == t) return c.number;
    }
    std::string lower = t;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const ElementCell& c : element_cells()) {
        std::string name = c.name;
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::string sym = c.symbol;
        for (char& ch : sym) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (name == lower || sym == lower) return c.number;
    }
    if (lower == "aluminium") return 13;
    if (lower == "sulphur") return 16;
    if (lower == "caesium") return 55;
    return 0;
}

std::string element_token(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == ' ' || c == '_') out += '-';
        else out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool element_allowed(const Node& node, int number) {
    const ElementCell* c = element_cell(number);
    if (!c) return false;
    if (node.has_attribute("disabled")) return false;
    if (symbol_listed(node, "also", number)) return true;
    if (symbol_listed(node, "disable", number)) return false;
    if (node.has_attribute("only") && !symbol_listed(node, "only", number)) return false;
    const std::string category = element_token(c->category);
    const std::string classification = element_token(c->classification);
    const std::string block(1, c->block);
    const auto check = [&](const char* only, const char* disable, auto&& has) {
        if (node.has_attribute(disable) && has(disable)) return false;
        if (node.has_attribute(only) && !has(only)) return false;
        return true;
    };
    if (!check("only-categories", "disable-categories", [&](const char* a) { return list_has(node, a, category); })) return false;
    if (!check("only-classes", "disable-classes", [&](const char* a) { return list_has(node, a, classification); })) return false;
    if (!check("only-blocks", "disable-blocks", [&](const char* a) { return list_has(node, a, block); })) return false;
    if (!check("only-groups", "disable-groups", [&](const char* a) { return list_has_number(node, a, c->group); })) return false;
    if (!check("only-periods", "disable-periods", [&](const char* a) { return list_has_number(node, a, c->period); })) return false;
    if (const Attribute* a = node.find_attribute("max-number")) {
        const int limit = std::atoi(a->value.c_str());
        if (limit > 0 && number > limit) return false;
    }
    if (const Attribute* a = node.find_attribute("min-number")) {
        const int limit = std::atoi(a->value.c_str());
        if (limit > 0 && number < limit) return false;
    }
    if (node.has_attribute("only-stable")) {
        const chem::Element& e = chem::PeriodicTable::element(number);
        if (!e.is_stable && e.atomic_number != 83 && e.atomic_number != 90 && e.atomic_number != 92) return false;
    }
    return true;
}

std::vector<int> element_selection(const Node& node) {
    std::vector<int> out;
    const Attribute* a = node.find_attribute("value");
    if (!a) return out;
    for (const std::string& w : words_of(a->value)) {
        const int n = element_number(w);
        if (n > 0 && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    if (!node.has_attribute("multiple") && out.size() > 1) out.resize(1);
    return out;
}

std::string element_selection_text(const std::vector<int>& numbers) {
    std::string out;
    for (int n : numbers) {
        const ElementCell* c = element_cell(n);
        if (!c) continue;
        if (!out.empty()) out += ' ';
        out += c->symbol;
    }
    return out;
}

bool set_element_selection(Document& document, Node& node, const std::vector<int>& numbers) {
    std::vector<int> keep;
    for (int n : numbers) {
        if (element_cell(n) && std::find(keep.begin(), keep.end(), n) == keep.end()) keep.push_back(n);
    }
    if (!node.has_attribute("multiple") && keep.size() > 1) keep.resize(1);
    const std::string text = element_selection_text(keep);
    if (node.attribute_or("value", "") == text) return false;
    node.set_attribute("value", text);
    document.touch();
    return true;
}

bool select_element(Document& document, Node& node, int number) {
    if (!element_allowed(node, number)) return false;
    std::vector<int> sel = element_selection(node);
    if (node.has_attribute("multiple")) {
        const auto it = std::find(sel.begin(), sel.end(), number);
        if (it != sel.end()) sel.erase(it);
        else sel.push_back(number);
        if (const Attribute* a = node.find_attribute("max-selected")) {
            const int limit = std::atoi(a->value.c_str());
            if (limit > 0 && static_cast<int>(sel.size()) > limit) sel.erase(sel.begin());
        }
    } else {
        if (sel.size() == 1 && sel.front() == number && node.has_attribute("deselect")) sel.clear();
        else sel = { number };
    }
    return set_element_selection(document, node, sel);
}

std::uint32_t element_category_color(const ElementCell& cell, std::string_view scheme) noexcept {
    if (scheme == "none") return 0x45475aFFu;
    if (scheme == "class" || scheme == "classification") {
        if (cell.classification == "metal") return 0x89b4faFFu;
        if (cell.classification == "metalloid") return 0xa6e3a1FFu;
        return 0xf9e2afFFu;
    }
    if (scheme == "block") {
        switch (cell.block) {
            case 's': return 0xf38ba8FFu;
            case 'p': return 0xf9e2afFFu;
            case 'd': return 0x89b4faFFu;
            default: return 0xa6e3a1FFu;
        }
    }
    const std::string& c = cell.category;
    if (c == "alkali metal") return 0xf38ba8FFu;
    if (c == "alkaline earth metal") return 0xfab387FFu;
    if (c == "transition metal") return 0x89b4faFFu;
    if (c == "post-transition metal") return 0x94e2d5FFu;
    if (c == "metalloid") return 0xa6e3a1FFu;
    if (c == "reactive nonmetal") return 0xf9e2afFFu;
    if (c == "halogen") return 0xf5c2e7FFu;
    if (c == "noble gas") return 0xcba6f7FFu;
    if (c == "lanthanide") return 0x74c7ecFFu;
    return 0xb4befeFFu;
}

int element_token_charge(std::string_view text) noexcept {
    std::string digits;
    int sign = 0;
    bool started = false;
    for (char c : text) {
        if (!started) {
            if (std::isalpha(static_cast<unsigned char>(c)) || c == ' ') continue;
            started = true;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        else if (c == '+') sign = 1;
        else if (c == '-') sign = -1;
    }
    if (sign == 0) return 0;
    const int n = digits.empty() ? 1 : std::atoi(digits.c_str());
    return sign * std::max(1, n);
}

std::string element_ion_token(int number, int charge) {
    const ElementCell* c = element_cell(number);
    if (!c) return std::string();
    if (charge == 0) return c->symbol;
    const int a = charge < 0 ? -charge : charge;
    return c->symbol + (a == 1 ? std::string() : std::to_string(a)) + (charge > 0 ? "+" : "-");
}

std::string element_ion_label(int number, int charge) {
    const ElementCell* c = element_cell(number);
    if (!c) return std::string();
    if (charge == 0) return c->symbol;
    static const char* const sup[] = { "\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4", "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9" };
    const int a = charge < 0 ? -charge : charge;
    std::string out = c->symbol;
    if (a > 1) {
        for (char d : std::to_string(a)) out += sup[d - '0'];
    }
    out += charge > 0 ? "\xE2\x81\xBA" : "\xE2\x81\xBB";
    return out;
}

bool element_has_charges(const Node& node) {
    return node.has_attribute("charges") && node.attribute_or("charges", "") != "false";
}

std::vector<int> element_charge_options(const Node& node, int number) {
    std::vector<int> out;
    if (!element_has_charges(node) || !element_cell(number)) return out;
    const std::vector<std::string> words = words_of(node.attribute_or("charges", ""));
    const auto has = [&](const char* w) { return std::find(words.begin(), words.end(), w) != words.end(); };
    const bool positive_only = has("positive") || has("cations");
    const bool negative_only = has("negative") || has("anions");
    const bool all = has("all");
    const auto keep = [&](int q) {
        if (q == 0) return;
        if (positive_only && q < 0) return;
        if (negative_only && q > 0) return;
        if (std::find(out.begin(), out.end(), q) == out.end()) out.push_back(q);
    };
    static const std::vector<chem::IonDescriptor> common = [] {
        std::vector<chem::IonDescriptor> v = chem::common_cations();
        const std::vector<chem::IonDescriptor> a = chem::common_anions();
        v.insert(v.end(), a.begin(), a.end());
        return v;
    }();
    for (const chem::IonDescriptor& d : common) {
        if (!d.polyatomic && d.element && d.element->atomic_number == number) keep(d.charge);
    }
    const chem::Element& e = chem::PeriodicTable::element(number);
    const bool none_yet = out.empty();
    for (const auto& s : e.oxidation_states()) {
        if (all || (none_yet && s.common)) keep(s.state);
    }
    std::sort(out.begin(), out.end(), [](int a, int b) {
        if ((a > 0) != (b > 0)) return a > 0;
        return std::abs(a) < std::abs(b);
    });
    if (has("neutral")) out.insert(out.begin(), 0);
    return out;
}

std::vector<std::pair<int, int>> element_ion_selection(const Node& node) {
    std::vector<std::pair<int, int>> out;
    const Attribute* a = node.find_attribute("value");
    if (!a) return out;
    for (const std::string& w : words_of(a->value)) {
        const int n = element_number(w);
        if (n <= 0) continue;
        bool dup = false;
        for (const auto& p : out) dup = dup || p.first == n;
        if (!dup) out.emplace_back(n, element_token_charge(w));
    }
    if (!node.has_attribute("multiple") && out.size() > 1) out.resize(1);
    return out;
}

bool select_element_ion(Document& document, Node& node, int number, int charge) {
    if (!element_allowed(node, number)) return false;
    std::vector<std::pair<int, int>> sel = element_ion_selection(node);
    if (node.has_attribute("multiple")) {
        bool replaced = false;
        for (auto& p : sel) {
            if (p.first == number) {
                p.second = charge;
                replaced = true;
            }
        }
        if (!replaced) sel.emplace_back(number, charge);
        if (const Attribute* a = node.find_attribute("max-selected")) {
            const int limit = std::atoi(a->value.c_str());
            if (limit > 0 && static_cast<int>(sel.size()) > limit) sel.erase(sel.begin());
        }
    } else {
        sel = { { number, charge } };
    }
    std::string text;
    for (const auto& p : sel) {
        if (!text.empty()) text += ' ';
        text += element_ion_token(p.first, p.second);
    }
    if (node.attribute_or("value", "") == text) return false;
    node.set_attribute("value", text);
    document.touch();
    return true;
}

std::vector<std::string> element_filter_problems(const Node& node) {
    std::vector<std::string> out;
    if (element_has_charges(node)) {
        for (const std::string& w : words_of(node.attribute_or("charges", ""))) {
            if (w != "positive" && w != "negative" && w != "cations" && w != "anions" && w != "both" && w != "all" && w != "common" && w != "neutral" && w != "true") out.push_back("charges: '" + w + "' should be positive, negative, both, common, all or neutral");
        }
    }
    static const char* const symbol_lists[] = { "value", "disable", "only", "also" };
    for (const char* name : symbol_lists) {
        const Attribute* a = node.find_attribute(name);
        if (!a) continue;
        for (const std::string& w : words_of(a->value)) {
            if (element_number(w) == 0) out.push_back(std::string(name) + ": '" + w + "' isn't an element");
        }
    }
    static const char* const categories[] = { "alkali-metal", "alkaline-earth-metal", "transition-metal", "post-transition-metal", "metalloid", "reactive-nonmetal", "halogen", "noble-gas", "lanthanide", "actinide" };
    for (const char* name : { "only-categories", "disable-categories" }) {
        const Attribute* a = node.find_attribute(name);
        if (!a) continue;
        for (const std::string& w : words_of(a->value)) {
            const std::string t = element_token(w);
            bool known = false;
            for (const char* c : categories) known = known || token_matches(t, c);
            known = known || t == "nonmetal" || t == "nonmetals" || t == "inner-transition-metal" || t == "inner-transition-metals";
            if (!known) out.push_back(std::string(name) + ": '" + w + "' isn't a category (use alkali-metal, alkaline-earth-metal, transition-metal, post-transition-metal, metalloid, reactive-nonmetal, halogen, noble-gas, lanthanide or actinide)");
        }
    }
    for (const char* name : { "only-classes", "disable-classes" }) {
        const Attribute* a = node.find_attribute(name);
        if (!a) continue;
        for (const std::string& w : words_of(a->value)) {
            const std::string t = element_token(w);
            if (!token_matches(t, "metal") && !token_matches(t, "metalloid") && t != "nonmetal" && t != "nonmetals") out.push_back(std::string(name) + ": '" + w + "' should be metal, metalloid or nonmetal");
        }
    }
    for (const char* name : { "only-blocks", "disable-blocks" }) {
        const Attribute* a = node.find_attribute(name);
        if (!a) continue;
        for (const std::string& w : words_of(a->value)) {
            if (w != "s" && w != "p" && w != "d" && w != "f") out.push_back(std::string(name) + ": '" + w + "' should be s, p, d or f");
        }
    }
    return out;
}

} // namespace grove
