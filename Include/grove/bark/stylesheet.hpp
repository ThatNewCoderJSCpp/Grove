#ifndef GROVE_BARK_STYLESHEET_HPP
#define GROVE_BARK_STYLESHEET_HPP

#include "value.hpp"
#include "../markup/node.hpp"

#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace grove {
namespace bark {

enum class Combinator {
    None,
    Descendant,
    Child
};

struct Compound {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    Combinator combinator = Combinator::None;

    bool matches(const Node& node) const;
};

struct Specificity {
    int ids = 0;
    int classes = 0;
    int tags = 0;

    bool operator<(const Specificity& o) const noexcept { return std::tie(ids, classes, tags) < std::tie(o.ids, o.classes, o.tags); }
    bool operator==(const Specificity& o) const noexcept { return std::tie(ids, classes, tags) == std::tie(o.ids, o.classes, o.tags); }
};

struct Selector {
    std::vector<Compound> parts;
    Span span;

    bool matches(const Node& node) const;
    Specificity specificity() const noexcept;
    std::string to_string() const;
};

struct Declaration {
    std::string property;
    ValueList values;
    bool important = false;
    Span span;
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    Span span;
};

class Stylesheet {
public:
    const std::string& path() const noexcept { return m_path; }
    void set_path(std::string path) { m_path = std::move(path); }

    std::vector<Rule>& rules() noexcept { return m_rules; }
    const std::vector<Rule>& rules() const noexcept { return m_rules; }

    std::vector<std::string>& imports() noexcept { return m_imports; }
    const std::vector<std::string>& imports() const noexcept { return m_imports; }

    std::map<std::string, ValueList>& variables() noexcept { return m_variables; }
    const std::map<std::string, ValueList>& variables() const noexcept { return m_variables; }

    void append(const Stylesheet& other);
    void dump(std::ostream& os) const;

private:
    std::string m_path;
    std::vector<Rule> m_rules;
    std::vector<std::string> m_imports;
    std::map<std::string, ValueList> m_variables;
};

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_STYLESHEET_HPP
