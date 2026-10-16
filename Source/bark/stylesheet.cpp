#include "grove/bark/stylesheet.hpp"

namespace grove {
namespace bark {

bool Compound::matches(const Node& node) const {
    if (!node.is_element()) return false;
    if (!tag.empty() && tag != "*" && tag != node.tag()) return false;
    if (!id.empty() && node.id() != id) return false;
    for (const std::string& c : classes) {
        if (!node.has_class(c)) return false;
    }
    return true;
}

namespace {

bool match_from(const std::vector<Compound>& parts, std::size_t index, const Node& node) {
    if (!parts[index].matches(node)) return false;
    if (index == 0) return true;
    const Combinator link = parts[index].combinator;
    const Node* p = node.parent();
    if (link == Combinator::Child) return p && match_from(parts, index - 1, *p);
    for (; p; p = p->parent()) {
        if (match_from(parts, index - 1, *p)) return true;
    }
    return false;
}

} // namespace

bool Selector::matches(const Node& node) const {
    return !parts.empty() && match_from(parts, parts.size() - 1, node);
}

Specificity Selector::specificity() const noexcept {
    Specificity s;
    for (const Compound& c : parts) {
        if (!c.id.empty()) ++s.ids;
        s.classes += static_cast<int>(c.classes.size());
        if (!c.tag.empty() && c.tag != "*") ++s.tags;
    }
    return s;
}

std::string Selector::to_string() const {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Compound& c = parts[i];
        if (i) out += c.combinator == Combinator::Child ? " > " : " ";
        out += c.tag;
        if (!c.id.empty()) out += "#" + c.id;
        for (const std::string& cls : c.classes) out += "." + cls;
        if (c.tag.empty() && c.id.empty() && c.classes.empty()) out += "*";
    }
    return out;
}

void Stylesheet::append(const Stylesheet& other) {
    m_rules.insert(m_rules.end(), other.m_rules.begin(), other.m_rules.end());
    m_imports.insert(m_imports.end(), other.m_imports.begin(), other.m_imports.end());
    for (const auto& [name, values] : other.m_variables) m_variables[name] = values;
}

void Stylesheet::dump(std::ostream& os) const {
    for (const auto& [name, values] : m_variables) os << '$' << name << " = " << to_string(values) << ";\n";
    for (const Rule& r : m_rules) {
        for (std::size_t i = 0; i < r.selectors.size(); ++i) {
            if (i) os << ", ";
            os << r.selectors[i].to_string();
        }
        os << " {\n";
        for (const Declaration& d : r.declarations) {
            os << "    " << d.property << ": " << to_string(d.values) << (d.important ? " !important" : "") << ";\n";
        }
        os << "}\n";
    }
}

} // namespace bark
} // namespace grove
