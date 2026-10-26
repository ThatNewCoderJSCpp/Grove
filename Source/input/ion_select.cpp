#include "grove/input/ion_select.hpp"

#include <fizmo/../../Source/Chemistry/acid_base.hpp>
#include <fizmo/../../Source/Chemistry/nomenclature.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>

namespace grove {

namespace {

namespace chem = fizmo::chemistry;

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<IonChoice> build_choices() {
    std::vector<IonChoice> out;
    for (const chem::PolyatomicIonEntry& e : chem::polyatomic_ions()) {
        IonChoice c;
        c.name = e.name;
        c.formula = chem::ion_formula_text(e, false);
        c.formula_text = chem::ion_formula_text(e, true);
        c.category = e.category;
        c.charge = e.charge;
        c.polyatomic = true;
        out.push_back(std::move(c));
    }
    std::vector<chem::IonDescriptor> mono = chem::common_cations();
    const std::vector<chem::IonDescriptor> anions = chem::common_anions();
    mono.insert(mono.end(), anions.begin(), anions.end());
    for (const chem::IonDescriptor& d : mono) {
        if (d.polyatomic || !d.element) continue;
        IonChoice c;
        c.name = d.name;
        c.formula = d.text(false);
        c.formula_text = d.text(true);
        c.category = "monatomic";
        c.charge = d.charge;
        c.polyatomic = false;
        const bool dup = std::any_of(out.begin(), out.end(), [&](const IonChoice& o) { return o.formula == c.formula && o.charge == c.charge; });
        if (!dup) out.push_back(std::move(c));
    }
    return out;
}

bool wants_charge(const std::string& mode, int charge) {
    if (mode == "positive" || mode == "positives" || mode == "cations" || mode == "cation" || mode == "+") return charge > 0;
    if (mode == "negative" || mode == "negatives" || mode == "anions" || mode == "anion" || mode == "-") return charge < 0;
    return true;
}

bool valid_mode(const std::string& mode) {
    for (const char* m : { "both", "all", "positive", "positives", "cations", "cation", "+", "negative", "negatives", "anions", "anion", "-" }) {
        if (mode == m) return true;
    }
    return false;
}

} // namespace

bool is_ion_select_tag(std::string_view tag) noexcept {
    return tag == "input_ion" || tag == "polyatomic_ion";
}

bool is_ion_select(const Node& node) noexcept {
    return node.is_element() && is_ion_select_tag(node.tag());
}

const std::vector<IonChoice>& ion_choices() {
    static const std::vector<IonChoice> choices = build_choices();
    return choices;
}

std::vector<IonChoice> ion_choices_for(const Node& node) {
    const std::string mode = lower(node.attribute_or("charges", "both"));
    const bool monatomic = node.has_attribute("monatomic") && node.attribute_or("monatomic", "") != "false";
    const std::vector<std::string> categories = words(node.attribute_or("categories", ""));
    std::vector<IonChoice> out;
    for (const IonChoice& c : ion_choices()) {
        if (!wants_charge(mode, c.charge)) continue;
        if (!c.polyatomic && !monatomic) continue;
        if (!categories.empty() && std::find(categories.begin(), categories.end(), lower(c.category)) == categories.end()) continue;
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const IonChoice& a, const IonChoice& b) {
        if ((a.charge > 0) != (b.charge > 0)) return a.charge > 0;
        if (a.polyatomic != b.polyatomic) return !a.polyatomic;
        return false;
    });
    return out;
}

std::vector<SelectOption> ion_select_options(const Node& node) {
    std::vector<SelectOption> out;
    const bool formula_values = lower(node.attribute_or("values", "name")) == "formula";
    for (const IonChoice& c : ion_choices_for(node)) {
        SelectOption o;
        o.value = formula_values ? c.formula : c.name;
        o.label = c.formula_text + "   " + c.name;
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<std::string> ion_select_problems(const Node& node) {
    std::vector<std::string> out;
    if (!is_ion_select(node)) return out;
    const std::string mode = lower(node.attribute_or("charges", "both"));
    if (!valid_mode(mode)) out.push_back("has charges=\"" + node.attribute_or("charges", "") + "\"; use positive, negative or both");
    const std::string values = lower(node.attribute_or("values", "name"));
    if (values != "name" && values != "formula") out.push_back("has values=\"" + values + "\"; use name or formula");
    std::vector<std::string> known;
    for (const IonChoice& c : ion_choices()) known.push_back(lower(c.category));
    for (const std::string& cat : words(node.attribute_or("categories", ""))) {
        if (std::find(known.begin(), known.end(), cat) == known.end()) out.push_back("lists category \"" + cat + "\", which no ion has");
    }
    if (ion_choices_for(node).empty()) out.push_back("has no ions left after its filters");
    return out;
}

bool is_acid_base_select_tag(std::string_view tag) noexcept {
    return tag == "input_acid" || tag == "input_base";
}

bool is_acid_base_select(const Node& node) noexcept {
    return node.is_element() && is_acid_base_select_tag(node.tag());
}

namespace {

int species_charge(const chem::AcidBaseSpecies& s) {
    if (s.kind.find("anion") != std::string::npos) return -static_cast<int>(std::lround(s.strong_base_per_mole));
    if (s.kind.find("cation") != std::string::npos) return 1;
    return 0;
}

std::string species_formula_text(const chem::AcidBaseSpecies& s) {
    static const char* const sub[] = { "\xE2\x82\x80", "\xE2\x82\x81", "\xE2\x82\x82", "\xE2\x82\x83", "\xE2\x82\x84", "\xE2\x82\x85", "\xE2\x82\x86", "\xE2\x82\x87", "\xE2\x82\x88", "\xE2\x82\x89" };
    static const char* const sup[] = { "\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4", "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9" };
    std::string out;
    std::string text = s.formula;
    int charge = species_charge(s);
    if (!text.empty() && (text.back() == '+' || text.back() == '-')) {
        charge = text.back() == '+' ? 1 : -1;
        text.pop_back();
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool after = i > 0 && (std::isalpha(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == ')' || std::isdigit(static_cast<unsigned char>(text[i - 1])));
        if (std::isdigit(static_cast<unsigned char>(c)) && after) out += sub[c - '0'];
        else out += c;
    }
    if (charge != 0) {
        const int a = charge < 0 ? -charge : charge;
        if (a > 1) out += sup[a];
        out += charge > 0 ? "\xE2\x81\xBA" : "\xE2\x81\xBB";
    }
    return out;
}

std::string species_formula_value(const chem::AcidBaseSpecies& s) {
    const int q = species_charge(s);
    if (q == 0 || (!s.formula.empty() && (s.formula.back() == '+' || s.formula.back() == '-'))) return s.formula;
    const int a = q < 0 ? -q : q;
    return s.formula + "^" + (a > 1 ? std::to_string(a) : std::string()) + (q > 0 ? "+" : "-");
}

bool wants_species(const Node& node, const chem::AcidBaseSpecies& s) {
    const bool acid = node.tag() == "input_acid";
    if (acid != s.is_acid()) return false;
    const std::string strength = lower(node.attribute_or("strength", "both"));
    if ((strength == "strong" && !s.strong) || (strength == "weak" && s.strong)) return false;
    const std::string ions = lower(node.attribute_or("ions", "true"));
    if ((ions == "false" || ions == "no") && species_charge(s) != 0) return false;
    if (ions == "only" && species_charge(s) == 0) return false;
    return true;
}

} // namespace

std::vector<SelectOption> acid_base_select_options(const Node& node) {
    std::vector<const chem::AcidBaseSpecies*> list;
    for (const chem::AcidBaseSpecies& s : chem::acid_base_species()) {
        if (wants_species(node, s)) list.push_back(&s);
    }
    std::stable_sort(list.begin(), list.end(), [](const chem::AcidBaseSpecies* a, const chem::AcidBaseSpecies* b) {
        if (a->strong != b->strong) return a->strong;
        const bool ia = species_charge(*a) != 0, ib = species_charge(*b) != 0;
        if (ia != ib) return !ia;
        return a->name < b->name;
    });
    const bool formula_values = lower(node.attribute_or("values", "name")) == "formula";
    std::vector<SelectOption> out;
    for (const chem::AcidBaseSpecies* s : list) {
        SelectOption o;
        o.value = formula_values ? species_formula_value(*s) : s->name;
        std::string label = species_formula_text(*s) + "   " + s->name + "  (" + (s->strong ? "strong" : "weak");
        if (s->is_acid() && s->equivalents > 1) label += ", " + std::to_string(s->equivalents) + " H\xE2\x81\xBA";
        if (s->is_base() && s->equivalents > 1) label += ", takes " + std::to_string(s->equivalents) + " H\xE2\x81\xBA";
        label += ")";
        o.label = label;
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<std::string> acid_base_select_problems(const Node& node) {
    std::vector<std::string> out;
    if (!is_acid_base_select(node)) return out;
    const std::string strength = lower(node.attribute_or("strength", "both"));
    if (strength != "both" && strength != "strong" && strength != "weak" && strength != "all") out.push_back("has strength=\"" + strength + "\"; use strong, weak or both");
    const std::string ions = lower(node.attribute_or("ions", "true"));
    if (ions != "true" && ions != "false" && ions != "no" && ions != "only" && ions != "yes" && !ions.empty()) out.push_back("has ions=\"" + ions + "\"; use true, false or only");
    const std::string values = lower(node.attribute_or("values", "name"));
    if (values != "name" && values != "formula") out.push_back("has values=\"" + values + "\"; use name or formula");
    if (acid_base_select_options(node).empty()) out.push_back("has nothing left after its filters");
    return out;
}

} // namespace grove
