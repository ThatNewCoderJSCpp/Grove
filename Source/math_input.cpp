#include "grove/math_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace grove {

namespace {

struct CodePoint {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string text;
};

std::vector<CodePoint> split(const std::string& s) {
    std::vector<CodePoint> out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        out.push_back(CodePoint{ i, j, s.substr(i, j - i) });
        i = j;
    }
    return out;
}

struct Named {
    const char* name;
    const char* symbol;
};

constexpr Named k_symbols[] = {
    { "alpha", "\xCE\xB1" }, { "beta", "\xCE\xB2" }, { "gamma", "\xCE\xB3" }, { "delta", "\xCE\xB4" }, { "epsilon", "\xCE\xB5" },
    { "varepsilon", "\xCE\xB5" }, { "zeta", "\xCE\xB6" }, { "eta", "\xCE\xB7" }, { "theta", "\xCE\xB8" }, { "vartheta", "\xCF\x91" },
    { "iota", "\xCE\xB9" }, { "kappa", "\xCE\xBA" }, { "lambda", "\xCE\xBB" }, { "mu", "\xCE\xBC" }, { "nu", "\xCE\xBD" },
    { "xi", "\xCE\xBE" }, { "omicron", "\xCE\xBF" }, { "pi", "\xCF\x80" }, { "varpi", "\xCF\x96" }, { "rho", "\xCF\x81" },
    { "varrho", "\xCF\xB1" }, { "sigma", "\xCF\x83" }, { "varsigma", "\xCF\x82" }, { "tau", "\xCF\x84" }, { "upsilon", "\xCF\x85" },
    { "phi", "\xCF\x95" }, { "varphi", "\xCF\x86" }, { "chi", "\xCF\x87" }, { "psi", "\xCF\x88" }, { "omega", "\xCF\x89" },
    { "Alpha", "\xCE\x91" }, { "Beta", "\xCE\x92" }, { "Gamma", "\xCE\x93" }, { "Delta", "\xCE\x94" }, { "Epsilon", "\xCE\x95" },
    { "Zeta", "\xCE\x96" }, { "Eta", "\xCE\x97" }, { "Theta", "\xCE\x98" }, { "Iota", "\xCE\x99" }, { "Kappa", "\xCE\x9A" },
    { "Lambda", "\xCE\x9B" }, { "Mu", "\xCE\x9C" }, { "Nu", "\xCE\x9D" }, { "Xi", "\xCE\x9E" }, { "Omicron", "\xCE\x9F" },
    { "Pi", "\xCE\xA0" }, { "Rho", "\xCE\xA1" }, { "Sigma", "\xCE\xA3" }, { "Tau", "\xCE\xA4" }, { "Upsilon", "\xCE\xA5" },
    { "Phi", "\xCE\xA6" }, { "Chi", "\xCE\xA7" }, { "Psi", "\xCE\xA8" }, { "Omega", "\xCE\xA9" },
    { "inf", "\xE2\x88\x9E" }, { "infinity", "\xE2\x88\x9E" }
};

constexpr const char* k_functions[] = {
    "sin", "cos", "tan", "sec", "csc", "cot", "asin", "acos", "atan", "asec", "acsc", "acot", "arcsin", "arccos", "arctan",
    "arcsec", "arccsc", "arccot", "sinh", "cosh", "tanh", "sech", "csch", "coth", "asinh", "acosh", "atanh", "arcsinh",
    "arccosh", "arctanh", "ln", "log", "exp", "min", "max", "abs", "floor", "ceil", "round", "sign", "sgn", "mod", "gcd",
    "lcm", "erf", "erfc", "re", "im", "arg", "conj", "det", "lim", "sum", "prod", "if"
};

const char* symbol_for(const std::string& word) {
    for (const Named& n : k_symbols) {
        if (word == n.name) return n.symbol;
    }
    return nullptr;
}

bool is_function_name(const std::string& word) {
    for (const char* f : k_functions) {
        if (word == f) return true;
    }
    return false;
}

bool ascii_letter(const std::string& c) {
    return c.size() == 1 && std::isalpha(static_cast<unsigned char>(c[0]));
}

bool ascii_digit(const std::string& c) {
    return c.size() == 1 && (std::isdigit(static_cast<unsigned char>(c[0])) || c[0] == '.');
}

bool binary_operator(const std::string& c) {
    static const char* const ops[] = { "+", "-", "=", "<", ">", "\xC2\xB7", "\xE2\x88\x92", "\xE2\x89\xA4", "\xE2\x89\xA5", "\xE2\x89\xA0", "\xC3\x97", "\xC3\xB7", ":" };
    for (const char* o : ops) {
        if (c == o) return true;
    }
    return false;
}

bool fraction_boundary(const std::string& c) {
    static const char* const ops[] = { "+", "-", "=", "<", ">", ",", "\xE2\x88\x92", "\xE2\x89\xA4", "\xE2\x89\xA5", "\xE2\x89\xA0", ":", ";", "|" };
    for (const char* o : ops) {
        if (c == o) return true;
    }
    return false;
}

constexpr std::size_t k_none = std::numeric_limits<std::size_t>::max();

enum class Kind { Glyph, Seq, Group, Script, Fraction, Radical, BigOp };

const char* big_operator_symbol(const std::string& word) {
    if (word == "sum") return "\xE2\x88\x91";
    if (word == "prod") return "\xE2\x88\x8F";
    if (word == "int") return "\xE2\x88\xAB";
    return nullptr;
}

struct Box {
    float w = 0.0f;
    float asc = 0.0f;
    float desc = 0.0f;
};

struct Item {
    Kind kind = Kind::Glyph;
    std::size_t cp = k_none;
    std::string shown;
    bool visible = true;
    bool italic = false;
    bool spaced = false;
    std::vector<std::unique_ptr<Item>> items;
    std::size_t open = k_none;
    std::size_t close = k_none;
    bool hide = false;
    bool stretch = false;
    std::unique_ptr<Item> base;
    std::size_t sup_marker = k_none;
    std::unique_ptr<Item> sup;
    std::size_t sub_marker = k_none;
    std::unique_ptr<Item> sub;
    std::unique_ptr<Item> num;
    std::unique_ptr<Item> den;
    std::size_t slash = k_none;
    std::size_t name_first = k_none;
    std::size_t name_last = k_none;
    std::size_t comma = k_none;
    std::unique_ptr<Item> inner;
    std::unique_ptr<Item> index;
    bool cube = false;
    Box box;
    float scale = 1.0f;
    float sup_up = 0.0f;
    float sub_down = 0.0f;
    float paren_w = 0.0f;
    float root_w = 0.0f;
    bool integral = false;
    float drop = 0.0f;
    bool stacked = false;
};

using ItemPtr = std::unique_ptr<Item>;

ItemPtr make(Kind k) {
    ItemPtr p = std::make_unique<Item>();
    p->kind = k;
    return p;
}

class Builder {
public:
    Builder(const std::string& text, const MathInputMetrics& m, bool chem = false) : m_cps(split(text)), m_m(m), m_chem(chem) {
        m_glyph_of.assign(m_cps.size(), k_none);
        m_caret_override.assign(m_cps.size(), Spot{});
    }

    MathInputLayout run() {
        std::size_t i = 0;
        ItemPtr top = make(Kind::Seq);
        if (m_chem) {
            chem_items(top->items);
        } else {
            while (i < m_cps.size()) {
                std::vector<ItemPtr> part = parse_seq(i, "");
                for (ItemPtr& p : part) top->items.push_back(std::move(p));
                if (i < m_cps.size()) top->items.push_back(glyph(i++));
            }
            fraction_pass(top->items);
            spacing_pass(top->items);
        }
        measure(*top, 1.0f);
        place(*top, 0.0f, 0.0f, 1.0f);
        for (std::size_t k = 0; k < m_cps.size(); ++k) {
            if (m_glyph_of[k] != k_none) continue;
            MathInputGlyph g;
            g.begin = m_cps[k].begin;
            g.end = m_cps[k].end;
            g.text = m_cps[k].text;
            g.x = top->box.w;
            g.width = width(g.text, 1.0f, false);
            top->box.w += g.width;
            m_glyph_of[k] = m_out.glyphs.size();
            m_out.glyphs.push_back(g);
        }
        m_out.width = top->box.w;
        m_out.ascent = std::max(top->box.asc, m_m.ascent);
        m_out.descent = std::max(top->box.desc, m_m.descent);
        m_out.base_ascent = m_m.ascent;
        m_out.base_descent = m_m.descent;
        for (std::size_t k = 0; k < m_cps.size(); ++k) {
            const Spot& o = m_caret_override[k];
            const MathInputGlyph& g = m_out.glyphs[m_glyph_of[k]];
            MathInputCaret c = o.set ? MathInputCaret{ m_cps[k].begin, o.x, o.y, o.s } : MathInputCaret{ m_cps[k].begin, g.x, g.y, g.scale };
            c.weak = (k > 0 && m_cps[k - 1].text == "}") || m_cps[k].text == "{";
            if (!g.visible && k > 0 && ascii_letter(m_cps[k - 1].text) && (ascii_letter(m_cps[k].text) || m_cps[k].text == "(")) c.weak = true;
            m_out.carets.push_back(c);
        }
        if (m_cps.empty()) {
            m_out.carets.push_back(MathInputCaret{ 0, 0.0f, 0.0f, 1.0f });
        } else {
            const MathInputGlyph& g = m_out.glyphs[m_glyph_of.back()];
            if (!g.visible && (m_cps.back().text == "}" || m_cps.back().text == ")")) m_out.carets.push_back(MathInputCaret{ m_cps.back().end, top->box.w, 0.0f, 1.0f });
            else m_out.carets.push_back(MathInputCaret{ m_cps.back().end, g.x + g.width, g.y, g.scale });
        }
        return std::move(m_out);
    }

private:
    const std::string& at(std::size_t i) const {
        static const std::string empty;
        return i < m_cps.size() ? m_cps[i].text : empty;
    }

    float width(const std::string& s, float scale, bool italic) const {
        if (s.empty()) return 0.0f;
        if (m_m.width) return m_m.width(s, scale, italic);
        float n = 0.0f;
        for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1.0f : 0.0f;
        return n * 0.55f * (m_m.ascent + m_m.descent) * scale;
    }

    float em(float scale) const { return (m_m.ascent + m_m.descent) * scale; }

    ItemPtr glyph(std::size_t i, const std::string& shown = std::string(), bool visible = true, bool italic = false) {
        ItemPtr g = make(Kind::Glyph);
        g->cp = i;
        g->shown = shown.empty() ? at(i) : shown;
        g->visible = visible;
        g->italic = italic;
        return g;
    }

    static bool is_stop(const std::string& c, const char* stops) {
        if (c.size() != 1) return false;
        return std::strchr(stops, c[0]) != nullptr && c[0] != '\0';
    }

    std::vector<ItemPtr> parse_seq(std::size_t& i, const char* stops) {
        std::vector<ItemPtr> items;
        while (i < m_cps.size() && !is_stop(at(i), stops)) items.push_back(parse_full(i, true, true));
        fraction_pass(items);
        spacing_pass(items);
        return items;
    }

    ItemPtr seq_of(std::vector<ItemPtr> items) {
        ItemPtr s = make(Kind::Seq);
        s->items = std::move(items);
        return s;
    }

    bool has_operand(std::size_t i) const {
        if (i >= m_cps.size()) return false;
        const std::string& c = at(i);
        if (c == ")" || c == "}" || c == "," || c == " " || c == "^" || c == "_") return false;
        if (c == "-" || c == "+") return i + 1 < m_cps.size() && has_operand(i + 1) && at(i + 1) != "-" && at(i + 1) != "+";
        return true;
    }

    ItemPtr parse_full(std::size_t& i, bool allow_sup, bool allow_sub) {
        ItemPtr base = parse_atom(i);
        ItemPtr script;
        for (int round = 0; round < 2 && i < m_cps.size(); ++round) {
            const std::string& c = at(i);
            const bool sup = c == "^" && allow_sup && (!script || script->sup_marker == k_none);
            const bool sub = c == "_" && allow_sub && (!script || script->sub_marker == k_none);
            if (!sup && !sub) break;
            if (!has_operand(i + 1)) break;
            if (!script) {
                script = make(Kind::Script);
                script->base = std::move(base);
            }
            const std::size_t marker = i++;
            if (sup) {
                script->sup_marker = marker;
                script->sup = parse_operand(i, true);
            } else {
                script->sub_marker = marker;
                script->sub = parse_operand(i, false);
            }
        }
        return script ? std::move(script) : std::move(base);
    }

    ItemPtr parse_operand(std::size_t& i, bool sup) {
        const std::string& c = at(i);
        if (c == "(" || c == "{") {
            ItemPtr g = make(Kind::Group);
            g->hide = c == "{";
            g->open = i++;
            std::vector<ItemPtr> inner = parse_seq(i, c == "(" ? ")" : "}");
            g->inner = seq_of(std::move(inner));
            if (i < m_cps.size()) g->close = i++;
            if (sup && i < m_cps.size() && at(i) == "^" && has_operand(i + 1)) return wrap_sup(std::move(g), i);
            return g;
        }
        if (c == "-" || c == "+") {
            std::vector<ItemPtr> parts;
            parts.push_back(glyph(i, c == "-" ? "\xE2\x88\x92" : c));
            ++i;
            parts.push_back(parse_operand(i, sup));
            return seq_of(std::move(parts));
        }
        ItemPtr token;
        if (ascii_digit(c)) {
            std::vector<ItemPtr> digits;
            while (i < m_cps.size() && ascii_digit(at(i))) digits.push_back(glyph(i++));
            token = seq_of(std::move(digits));
        } else if (ascii_letter(c)) {
            std::size_t j = i;
            std::string word;
            while (j < m_cps.size() && ascii_letter(at(j))) word += at(j++);
            if (symbol_for(word) || ((word == "sqrt" || word == "cbrt" || word == "nthroot") && at(j) == "(") || (word.size() > 1 && at(j) == "(")) {
                token = parse_atom(i);
            } else {
                token = glyph(i, std::string(), true, true);
                ++i;
            }
        } else {
            token = parse_atom(i);
        }
        if (sup && i < m_cps.size() && at(i) == "^" && has_operand(i + 1)) return wrap_sup(std::move(token), i);
        return token;
    }

    ItemPtr wrap_sup(ItemPtr base, std::size_t& i) {
        ItemPtr s = make(Kind::Script);
        s->base = std::move(base);
        s->sup_marker = i++;
        s->sup = parse_operand(i, true);
        return s;
    }

    ItemPtr parse_atom(std::size_t& i) {
        const std::string c = at(i);
        if (ascii_letter(c)) {
            std::size_t j = i;
            std::string word;
            while (j < m_cps.size() && ascii_letter(at(j))) word += at(j++);
            const bool call = at(j) == "(";
            if (call && (word == "sqrt" || word == "cbrt" || word == "nthroot")) return parse_radical(i, j, word);
            if (const char* big = big_operator_symbol(word)) {
                if (at(j) == "_" || at(j) == "^") {
                    ItemPtr b = make(Kind::BigOp);
                    b->cp = i;
                    b->shown = big;
                    b->name_first = i;
                    b->name_last = j - 1;
                    b->integral = word == "int";
                    i = j;
                    return b;
                }
            }
            if (word == "gamma" && call) {
                std::vector<ItemPtr> parts;
                parts.push_back(glyph(i, "\xCE\x93"));
                for (std::size_t k = i + 1; k < j; ++k) parts.push_back(glyph(k, std::string(), false));
                i = j;
                return seq_of(std::move(parts));
            }
            if (const char* sym = symbol_for(word)) {
                std::vector<ItemPtr> parts;
                const bool upper = std::isupper(static_cast<unsigned char>(word[0])) != 0 || word == "inf" || word == "infinity";
                parts.push_back(glyph(i, sym, true, !upper));
                for (std::size_t k = i + 1; k < j; ++k) parts.push_back(glyph(k, std::string(), false));
                i = j;
                return seq_of(std::move(parts));
            }
            if (is_function_name(word) || (word.size() > 1 && call)) {
                std::vector<ItemPtr> parts;
                for (std::size_t k = i; k < j; ++k) parts.push_back(glyph(k));
                i = j;
                return seq_of(std::move(parts));
            }
            return glyph(i++, std::string(), true, true);
        }
        if (ascii_digit(c)) {
            std::vector<ItemPtr> digits;
            while (i < m_cps.size() && ascii_digit(at(i))) digits.push_back(glyph(i++));
            return seq_of(std::move(digits));
        }
        if (c == "(" || c == "[") {
            ItemPtr g = make(Kind::Group);
            g->open = i++;
            std::vector<ItemPtr> inner = parse_seq(i, c == "(" ? ")" : "]");
            g->inner = seq_of(std::move(inner));
            if (i < m_cps.size()) g->close = i++;
            return g;
        }
        if (c == "{") {
            ItemPtr g = make(Kind::Group);
            g->hide = true;
            g->open = i++;
            std::vector<ItemPtr> inner = parse_seq(i, "}");
            g->inner = seq_of(std::move(inner));
            if (i < m_cps.size()) g->close = i++;
            return g;
        }
        if ((c == "<" || c == ">" || c == "!") && at(i + 1) == "=") {
            std::vector<ItemPtr> parts;
            parts.push_back(glyph(i, c == "<" ? "\xE2\x89\xA4" : c == ">" ? "\xE2\x89\xA5" : "\xE2\x89\xA0"));
            parts.push_back(glyph(i + 1, std::string(), false));
            parts.front()->spaced = true;
            i += 2;
            ItemPtr s = seq_of(std::move(parts));
            s->shown = "op";
            return s;
        }
        if (c == "*") return glyph(i++, "\xC2\xB7");
        if (c == "-") return glyph(i++, "\xE2\x88\x92");
        if (c == ",") return glyph(i++, ", ");
        return glyph(i++);
    }

    ItemPtr parse_radical(std::size_t& i, std::size_t paren, const std::string& word) {
        ItemPtr r = make(Kind::Radical);
        r->name_first = i;
        r->name_last = paren - 1;
        r->cube = word == "cbrt";
        r->open = paren;
        i = paren + 1;
        std::vector<ItemPtr> inner = parse_seq(i, word == "nthroot" ? ",)" : ")");
        r->inner = seq_of(std::move(inner));
        if (word == "nthroot" && i < m_cps.size() && at(i) == ",") {
            r->comma = i++;
            std::vector<ItemPtr> index = parse_seq(i, ")");
            r->index = seq_of(std::move(index));
        }
        if (i < m_cps.size() && at(i) == ")") r->close = i++;
        return r;
    }

    static const std::string* glyph_text(const Item& it) {
        if (it.kind == Kind::Glyph) return &it.shown;
        if (it.kind == Kind::Seq && it.shown == "op" && !it.items.empty()) return &it.items.front()->shown;
        return nullptr;
    }

    void fraction_pass(std::vector<ItemPtr>& items) {
        for (std::size_t k = 0; k < items.size(); ++k) {
            Item& it = *items[k];
            if (it.kind != Kind::Glyph || it.shown != "/" || k + 1 >= items.size() || k == 0) continue;
            const std::string* next = glyph_text(*items[k + 1]);
            if (next && (fraction_boundary(*next) || *next == "/" || *next == ")")) continue;
            std::size_t start = k;
            const Item& before = *items[k - 1];
            const bool braced = before.kind == Kind::Group && before.hide && before.open != k_none && m_cps[before.open].text == "{";
            if (braced) start = k - 1;
            while (!braced && start > 0) {
                const std::string* t = glyph_text(*items[start - 1]);
                if (t && (fraction_boundary(*t) || *t == "\xC2\xB7" || *t == ", ")) break;
                --start;
            }
            if (start == k) continue;
            ItemPtr f = make(Kind::Fraction);
            f->slash = it.cp;
            std::vector<ItemPtr> num;
            for (std::size_t q = start; q < k; ++q) num.push_back(std::move(items[q]));
            f->num = seq_of(std::move(num));
            f->den = std::move(items[k + 1]);
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(start), items.begin() + static_cast<std::ptrdiff_t>(k + 2));
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(start), std::move(f));
            k = start;
        }
    }

    void spacing_pass(std::vector<ItemPtr>& items) {
        bool after_operator = true;
        for (ItemPtr& p : items) {
            Item* g = p.get();
            if (g->kind == Kind::Seq && g->shown == "op") g = g->items.front().get();
            const bool op = g->kind == Kind::Glyph && binary_operator(g->shown);
            if (op) {
                g->spaced = !after_operator || g->shown == "=" || g->shown == "<" || g->shown == ">" || g->shown == "\xE2\x89\xA4" || g->shown == "\xE2\x89\xA5" || g->shown == "\xE2\x89\xA0";
                after_operator = true;
            } else {
                const bool open = g->kind == Kind::Glyph && (g->shown == "(" || g->shown == ", ");
                after_operator = open;
            }
        }
    }

    Box measure(Item& it, float s) {
        it.scale = s;
        Box b;
        switch (it.kind) {
            case Kind::Glyph: {
                const float pad = it.spaced ? 0.16f * em(s) : 0.0f;
                b.w = it.visible ? width(it.shown, s, it.italic) + 2.0f * pad : 0.0f;
                b.asc = m_m.ascent * s;
                b.desc = m_m.descent * s;
                break;
            }
            case Kind::Seq: {
                b.asc = 0.0f;
                b.desc = 0.0f;
                for (ItemPtr& c : it.items) {
                    const Box cb = measure(*c, s);
                    b.w += cb.w;
                    b.asc = std::max(b.asc, cb.asc);
                    b.desc = std::max(b.desc, cb.desc);
                }
                if (it.items.empty()) {
                    b.asc = m_m.ascent * s;
                    b.desc = m_m.descent * s;
                }
                break;
            }
            case Kind::Group: {
                const Box in = measure(*it.inner, s);
                b = in;
                b.asc = std::max(b.asc, m_m.ascent * s);
                b.desc = std::max(b.desc, m_m.descent * s);
                if (!it.hide) {
                    it.stretch = in.asc + in.desc > 1.3f * em(s);
                    if (it.stretch) {
                        it.paren_w = 0.32f * em(s);
                        b.w += it.paren_w * (it.close != k_none ? 2.0f : 1.0f);
                        b.asc += 0.05f * em(s);
                        b.desc += 0.05f * em(s);
                    } else {
                        b.w += width(at(it.open), s, false);
                        if (it.close != k_none) b.w += width(at(it.close), s, false);
                    }
                }
                if (it.inner->items.empty() && it.hide) b.w = std::max(b.w, width("\xE2\x96\xA1", 0.8f * s, false) + 0.1f * em(s));
                break;
            }
            case Kind::BigOp: {
                const float bs = s * (it.integral ? 1.7f : 1.45f);
                it.drop = 0.3f * em(s) * (it.integral ? 1.0f : 0.7f);
                b.w = width(it.shown, bs, false) + 0.08f * em(s);
                b.asc = std::max(m_m.ascent * s, m_m.ascent * bs - it.drop);
                b.desc = std::max(m_m.descent * s, m_m.descent * bs + it.drop);
                break;
            }
            case Kind::Script: {
                const Box base = measure(*it.base, s);
                const float ss = std::max(0.42f, s * 0.7f);
                it.stacked = it.base->kind == Kind::BigOp;
                if (it.stacked) {
                    const float gap = 0.08f * em(s);
                    b = base;
                    float w = base.w;
                    if (it.sup) {
                        const Box sb = measure(*it.sup, ss);
                        it.sup_up = base.asc + gap + sb.desc;
                        b.asc = it.sup_up + sb.asc;
                        w = std::max(w, sb.w);
                    }
                    if (it.sub) {
                        const Box sb = measure(*it.sub, ss);
                        it.sub_down = base.desc + gap + sb.asc;
                        b.desc = it.sub_down + sb.desc;
                        w = std::max(w, sb.w);
                    }
                    b.w = w + 0.12f * em(s);
                    break;
                }
                b = base;
                float extra = 0.0f;
                if (it.sup) {
                    const Box sb = measure(*it.sup, ss);
                    it.sup_up = std::max(0.42f * m_m.ascent * s, base.asc - 0.55f * m_m.ascent * s);
                    b.asc = std::max(b.asc, it.sup_up + sb.asc);
                    extra = std::max(extra, sb.w);
                }
                if (it.sub) {
                    const Box sb = measure(*it.sub, ss);
                    it.sub_down = std::max(0.3f * m_m.ascent * s, base.desc - 0.1f * m_m.descent * s);
                    b.desc = std::max(b.desc, it.sub_down + sb.desc);
                    extra = std::max(extra, sb.w);
                }
                b.w += extra + 0.04f * em(s);
                break;
            }
            case Kind::Fraction: {
                const float fs = std::max(0.4f, s >= 0.99f ? s * 0.9f : s * 0.75f);
                const Box nb = measure(*it.num, fs);
                const Box db = measure(*it.den, fs);
                const float axis = 0.3f * m_m.ascent * s;
                const float gap = 0.12f * em(s);
                const float th = std::max(1.0f, 0.06f * em(s));
                const float pad = 0.12f * em(s);
                b.w = std::max(nb.w, db.w) + 2.0f * pad;
                b.asc = axis + gap + nb.desc + nb.asc;
                b.desc = -axis + th + gap + db.asc + db.desc;
                b.desc = std::max(b.desc, m_m.descent * s);
                break;
            }
            case Kind::Radical: {
                const Box in = measure(*it.inner, s);
                Box inner = in;
                if (it.inner->items.empty()) inner.w = 0.45f * em(s);
                float index_w = 0.0f;
                if (it.index) index_w = measure(*it.index, std::max(0.35f, s * 0.5f)).w;
                if (it.cube) index_w = width("3", std::max(0.35f, s * 0.5f), false);
                const float rw0 = 0.55f * em(s);
                it.root_w = rw0 + std::max(0.0f, index_w - 0.3f * rw0);
                const float gap = 0.12f * em(s);
                const float th = std::max(1.0f, 0.06f * em(s));
                b.w = it.root_w + inner.w + 0.12f * em(s);
                b.asc = std::max(inner.asc, m_m.ascent * s) + gap + th;
                b.desc = std::max(inner.desc, m_m.descent * s);
                break;
            }
        }
        it.box = b;
        return b;
    }

    void put(std::size_t cp, const std::string& shown, bool visible, bool italic, float x, float y, float s, float w, float pad) {
        if (cp == k_none || cp >= m_cps.size()) return;
        MathInputGlyph g;
        g.begin = m_cps[cp].begin;
        g.end = m_cps[cp].end;
        g.text = shown;
        g.visible = visible;
        g.italic = italic;
        g.x = x;
        g.y = y;
        g.scale = s;
        g.width = w;
        g.pad = pad;
        m_glyph_of[cp] = m_out.glyphs.size();
        m_out.glyphs.push_back(g);
    }

    void decoration(const std::string& text, float x, float y, float s) {
        MathInputGlyph g;
        g.source = false;
        g.text = text;
        g.x = x;
        g.y = y;
        g.scale = s;
        g.width = width(text, s, false);
        m_out.glyphs.push_back(g);
    }

    void line(float x1, float y1, float x2, float y2, float th) { m_out.lines.push_back(MathInputLine{ x1, y1, x2, y2, th }); }

    void paren(float x, float top, float bottom, float w, bool left, float th) {
        const int n = 10;
        const float mid = 0.5f * (top + bottom);
        const float half = 0.5f * (bottom - top);
        float px = 0.0f;
        float py = 0.0f;
        for (int k = 0; k <= n; ++k) {
            const float t = -1.0f + 2.0f * static_cast<float>(k) / n;
            const float bulge = (1.0f - t * t) * w * 0.55f;
            const float cx = left ? x + w * 0.75f - bulge : x + w * 0.25f + bulge;
            const float cy = mid + t * half;
            if (k > 0) line(px, py, cx, cy, th);
            px = cx;
            py = cy;
        }
    }

    void place(Item& it, float x, float y, float s) {
        switch (it.kind) {
            case Kind::Glyph: {
                const float pad = it.spaced ? 0.16f * em(s) : 0.0f;
                put(it.cp, it.shown, it.visible, it.italic, x, y, s, it.box.w, pad);
                break;
            }
            case Kind::Seq: {
                float cx = x;
                for (ItemPtr& c : it.items) {
                    place(*c, cx, y, s);
                    cx += c->box.w;
                }
                break;
            }
            case Kind::Group: {
                float cx = x;
                const float th = std::max(1.0f, 0.06f * em(s));
                if (it.hide) {
                    put(it.open, at(it.open), false, false, cx, y, s, 0.0f, 0.0f);
                } else if (it.stretch) {
                    put(it.open, at(it.open), false, false, cx, y, s, it.paren_w, 0.0f);
                    paren(cx, y - it.box.asc, y + it.box.desc, it.paren_w, true, th);
                    cx += it.paren_w;
                } else {
                    const float w = width(at(it.open), s, false);
                    put(it.open, at(it.open), true, false, cx, y, s, w, 0.0f);
                    cx += w;
                }
                place(*it.inner, cx, y, s);
                cx += it.inner->box.w;
                if (it.inner->items.empty() && it.hide) {
                    const float ps = 0.8f * s;
                    const float pw = width("\xE2\x96\xA1", ps, false);
                    decoration("\xE2\x96\xA1", x + 0.5f * (it.box.w - pw), y, ps);
                    cx = x + it.box.w;
                    if (it.open < m_caret_override.size() && !m_caret_override[it.open].set) m_caret_override[it.open] = Spot{ true, cx, y, s };
                }
                if (it.close != k_none) {
                    if (it.hide) {
                        put(it.close, at(it.close), false, false, cx, y, s, 0.0f, 0.0f);
                    } else if (it.stretch) {
                        put(it.close, at(it.close), false, false, cx, y, s, it.paren_w, 0.0f);
                        paren(cx, y - it.box.asc, y + it.box.desc, it.paren_w, false, th);
                    } else {
                        put(it.close, at(it.close), true, false, cx, y, s, width(at(it.close), s, false), 0.0f);
                    }
                }
                break;
            }
            case Kind::BigOp: {
                const float bs = s * (it.integral ? 1.7f : 1.45f);
                put(it.name_first, it.shown, true, false, x, y + it.drop, bs, it.box.w, 0.0f);
                for (std::size_t k = it.name_first + 1; k <= it.name_last && k != k_none; ++k) put(k, at(k), false, false, x + it.box.w, y, s, 0.0f, 0.0f);
                break;
            }
            case Kind::Script: {
                if (it.stacked) {
                    const float inner = it.box.w - 0.12f * em(s);
                    const float ss = std::max(0.42f, s * 0.7f);
                    place(*it.base, x + 0.5f * (inner - it.base->box.w), y, s);
                    record_script(it);
                    if (it.sup) {
                        const float sx = x + 0.5f * (inner - it.sup->box.w);
                        put(it.sup_marker, "^", false, false, sx, y - it.sup_up, ss, 0.0f, 0.0f);
                        if (it.sup_marker < m_caret_override.size()) {
                            if (it.sub) m_caret_override[it.sup_marker] = Spot{ true, x + 0.5f * (inner - it.sub->box.w) + it.sub->box.w, y + it.sub_down, ss };
                            else m_caret_override[it.sup_marker] = Spot{ true, x + inner, y, s };
                        }
                        place(*it.sup, sx, y - it.sup_up, ss);
                    }
                    if (it.sub) {
                        const float sx = x + 0.5f * (inner - it.sub->box.w);
                        put(it.sub_marker, "_", false, false, sx, y + it.sub_down, ss, 0.0f, 0.0f);
                        if (it.sub_marker < m_caret_override.size()) m_caret_override[it.sub_marker] = Spot{ true, x + inner, y, s };
                        place(*it.sub, sx, y + it.sub_down, ss);
                    }
                    break;
                }
                place(*it.base, x, y, s);
                for (std::size_t marker : { it.sup_marker, it.sub_marker }) {
                    if (marker != k_none && marker < m_caret_override.size()) m_caret_override[marker] = Spot{ true, x + it.base->box.w, y, s };
                }
                record_script(it);
                const float sx = x + it.base->box.w + 0.02f * em(s);
                const float ss = std::max(0.42f, s * 0.7f);
                if (it.sup) {
                    put(it.sup_marker, "^", false, false, sx, y - it.sup_up, ss, 0.0f, 0.0f);
                    place(*it.sup, sx, y - it.sup_up, ss);
                }
                if (it.sub) {
                    put(it.sub_marker, "_", false, false, sx, y + it.sub_down, ss, 0.0f, 0.0f);
                    place(*it.sub, sx, y + it.sub_down, ss);
                    if (it.sup && it.sub_marker < it.sup_marker && it.sup_marker < m_caret_override.size()) m_caret_override[it.sup_marker] = Spot{ true, sx + it.sub->box.w, y + it.sub_down, ss };
                }
                if (it.sup && it.sub && it.sup_marker < it.sub_marker && it.sub_marker < m_caret_override.size()) {
                    m_caret_override[it.sub_marker] = Spot{ true, sx + it.sup->box.w, y - it.sup_up, ss };
                }
                break;
            }
            case Kind::Fraction: {
                const float fs = std::max(0.4f, s >= 0.99f ? s * 0.9f : s * 0.75f);
                const float axis = 0.3f * m_m.ascent * s;
                const float gap = 0.12f * em(s);
                const float th = std::max(1.0f, 0.06f * em(s));
                const float pad = 0.12f * em(s);
                const float inner_w = it.box.w - 2.0f * pad;
                const float num_base = y - axis - gap - it.num->box.desc;
                const float den_base = y - axis + th + gap + it.den->box.asc;
                const float nx = x + pad + 0.5f * (inner_w - it.num->box.w);
                const float dx = x + pad + 0.5f * (inner_w - it.den->box.w);
                place(*it.num, nx, num_base, fs);
                if (const Item* g = braced(*it.num)) {
                    if (g->open != k_none) m_caret_override[g->open] = Spot{ true, x, y, s };
                }
                record_fraction(it);
                put(it.slash, "/", false, false, nx + it.num->box.w, num_base, fs, 0.0f, 0.0f);
                place(*it.den, dx, den_base, fs);
                const float bar_y = y - axis + 0.5f * th;
                line(x + 0.5f * pad, bar_y, x + it.box.w - 0.5f * pad, bar_y, th);
                break;
            }
            case Kind::Radical: {
                for (std::size_t k = it.name_first; k <= it.name_last && k != k_none; ++k) put(k, at(k), false, false, x, y, s, 0.0f, 0.0f);
                const float th = std::max(1.0f, 0.06f * em(s));
                const float gap = 0.12f * em(s);
                const float rw0 = 0.55f * em(s);
                const float xs = x + it.root_w - rw0;
                const float inner_x = x + it.root_w;
                const float top = y - std::max(it.inner->box.asc, m_m.ascent * s) - gap - 0.5f * th;
                const float bottom = y + std::max(it.inner->box.desc, m_m.descent * s * 0.6f);
                const float h = bottom - top;
                const float end = x + it.box.w - 0.04f * em(s);
                line(xs, top + 0.62f * h, xs + 0.18f * rw0, top + 0.55f * h, th);
                line(xs + 0.18f * rw0, top + 0.55f * h, xs + 0.45f * rw0, bottom, th * 1.4f);
                line(xs + 0.45f * rw0, bottom, xs + 0.92f * rw0, top, th);
                line(xs + 0.92f * rw0, top, end, top, th);
                put(it.open, at(it.open), false, false, inner_x, y, s, 0.0f, 0.0f);
                place(*it.inner, inner_x, y, s);
                float cx = inner_x + (it.inner->items.empty() ? 0.45f * em(s) : it.inner->box.w);
                const float is = std::max(0.35f, s * 0.5f);
                const float index_base = top + 0.5f * h - 0.05f * em(s);
                if (it.cube) decoration("3", xs + 0.18f * rw0 - width("3", is, false), index_base, is);
                if (it.comma != k_none) put(it.comma, ",", false, false, cx, y, s, 0.0f, 0.0f);
                if (it.index) place(*it.index, std::max(x, xs + 0.2f * rw0 - it.index->box.w), index_base, is);
                if (it.close != k_none) put(it.close, ")", false, false, cx, y, s, 0.0f, 0.0f);
                break;
            }
        }
    }

    struct Spot {
        bool set = false;
        float x = 0.0f;
        float y = 0.0f;
        float s = 1.0f;
    };

    void span(const Item& it, std::size_t& lo, std::size_t& hi) const {
        for (std::size_t c : { it.cp, it.open, it.close, it.sup_marker, it.sub_marker, it.slash, it.name_first, it.name_last, it.comma }) {
            if (c == k_none) continue;
            lo = std::min(lo, c);
            hi = hi == k_none ? c : std::max(hi, c);
        }
        for (const ItemPtr& c : it.items) span(*c, lo, hi);
        for (const Item* c : { it.base.get(), it.sup.get(), it.sub.get(), it.num.get(), it.den.get(), it.inner.get(), it.index.get() }) {
            if (c) span(*c, lo, hi);
        }
    }

    std::size_t end_offset() const { return m_cps.empty() ? 0 : m_cps.back().end; }

    const Item* braced(const Item& it) const {
        const Item* p = &it;
        while (p->kind == Kind::Seq && p->items.size() == 1) p = p->items.front().get();
        return p->kind == Kind::Group && p->hide ? p : nullptr;
    }

    bool content(const Item& it, std::size_t& a, std::size_t& b) const {
        if (const Item* g = braced(it)) {
            if (g->open == k_none) return false;
            a = m_cps[g->open].end;
            b = g->close != k_none ? m_cps[g->close].begin : end_offset();
            return true;
        }
        std::size_t lo = k_none;
        std::size_t hi = k_none;
        span(it, lo, hi);
        if (lo == k_none || hi == k_none) return false;
        a = m_cps[lo].begin;
        b = m_cps[hi].end;
        return true;
    }

    void record_script(const Item& it) {
        MathInputRegion r;
        std::size_t lo = k_none;
        std::size_t hi = k_none;
        span(it, lo, hi);
        if (hi == k_none) return;
        r.after = m_cps[hi].end;
        if (it.sup) r.has_upper = content(*it.sup, r.upper_begin, r.upper_end);
        if (it.sub) r.has_lower = content(*it.sub, r.lower_begin, r.lower_end);
        if (r.has_upper || r.has_lower) m_out.regions.push_back(r);
    }

    void record_fraction(const Item& it) {
        MathInputRegion r;
        r.fraction = true;
        std::size_t lo = k_none;
        std::size_t hi = k_none;
        span(it, lo, hi);
        if (hi == k_none) return;
        r.after = m_cps[hi].end;
        r.has_upper = content(*it.num, r.upper_begin, r.upper_end);
        r.has_lower = content(*it.den, r.lower_begin, r.lower_end);
        if (r.has_upper && r.has_lower) m_out.regions.push_back(r);
    }


    bool chem_space(std::size_t i) const { return at(i) == " " || at(i) == "\t"; }

    bool chem_arrow(std::size_t i, std::size_t& length, std::string& shown) const {
        if (at(i) == "-" && at(i + 1) == ">") { length = 2; shown = "\xE2\x86\x92"; return true; }
        if (at(i) == "=" && at(i + 1) == ">") { length = 2; shown = "\xE2\x86\x92"; return true; }
        if (at(i) == "<" && (at(i + 1) == "=" || at(i + 1) == "-") && at(i + 2) == ">") { length = 3; shown = "\xE2\x87\x8C"; return true; }
        if (at(i) == "\xE2\x86\x92" || at(i) == "\xE2\x87\x8C" || at(i) == "\xE2\x87\x84" || at(i) == "\xE2\x86\x94") { length = 1; shown = at(i); return true; }
        if (at(i) == "=") { length = 1; shown = "="; return true; }
        return false;
    }

    static bool chem_sign(const std::string& c) { return c == "+" || c == "-" || c == "\xE2\x88\x92"; }

    bool chem_charge_end(std::size_t i) const {
        if (i >= m_cps.size()) return true;
        const std::string& c = at(i);
        return chem_space(i) || c == "}" || c == ")" || c == "," || c == ";" || chem_sign(c) || (c == "(" && (at(i + 1) == "a" || at(i + 1) == "s" || at(i + 1) == "l" || at(i + 1) == "g") );
    }

    std::size_t chem_token_end(std::size_t i) const {
        int braces = 0;
        std::size_t j = i;
        while (j < m_cps.size()) {
            const std::string& c = at(j);
            if (c == "{") ++braces;
            else if (c == "}") braces = std::max(0, braces - 1);
            if (braces == 0) {
                if (chem_space(j)) break;
                std::size_t len = 0;
                std::string shown;
                if (j > i && chem_arrow(j, len, shown)) break;
                if (c == "+" && j > i && at(j - 1) != "^" && !chem_charge_end(j + 1)) break;
            }
            ++j;
        }
        return j;
    }

    void chem_glyphs(std::size_t a, std::size_t b, std::vector<ItemPtr>& out) {
        for (std::size_t k = a; k < b; ++k) out.push_back(glyph(k, chem_sign(at(k)) && at(k) == "-" ? std::string("\xE2\x88\x92") : std::string()));
    }

    ItemPtr chem_script_operand(std::size_t& i, std::size_t end, bool charge) {
        if (at(i) == "{") {
            ItemPtr g = make(Kind::Group);
            g->hide = true;
            g->open = i++;
            std::vector<ItemPtr> inner;
            while (i < end && at(i) != "}") {
                inner.push_back(glyph(i, at(i) == "-" ? std::string("\xE2\x88\x92") : std::string()));
                ++i;
            }
            g->inner = seq_of(std::move(inner));
            if (i < end && at(i) == "}") g->close = i++;
            return g;
        }
        std::vector<ItemPtr> run;
        while (i < end && (ascii_digit(at(i)) || (charge && chem_sign(at(i))))) {
            run.push_back(glyph(i, at(i) == "-" ? std::string("\xE2\x88\x92") : std::string()));
            ++i;
        }
        return seq_of(std::move(run));
    }

    void chem_attach(std::vector<ItemPtr>& items, ItemPtr operand, std::size_t marker, bool sup) {
        if (items.empty()) {
            items.push_back(std::move(operand));
            return;
        }
        ItemPtr last = std::move(items.back());
        items.pop_back();
        ItemPtr script;
        if (last->kind == Kind::Script && ((sup && !last->sup) || (!sup && !last->sub && !last->sup))) script = std::move(last);
        else {
            script = make(Kind::Script);
            script->base = std::move(last);
        }
        if (sup) {
            script->sup_marker = marker;
            script->sup = std::move(operand);
        } else {
            script->sub_marker = marker;
            script->sub = std::move(operand);
        }
        items.push_back(std::move(script));
    }

    std::size_t chem_charge_start(std::size_t a, std::size_t b) const {
        int depth = 0;
        for (std::size_t k = b; k-- > a;) {
            if (at(k) == "}") ++depth;
            else if (at(k) == "{") --depth;
            else if (at(k) == "^" && depth == 0) return k;
        }
        std::size_t e = b;
        std::size_t signs = 0;
        while (e > a && chem_sign(at(e - 1))) {
            --e;
            ++signs;
        }
        if (signs == 0 || e == a) return k_none;
        std::size_t d = e;
        while (d > a && ascii_digit(at(d - 1)) && at(d - 1) != ".") --d;
        if (d == e || signs > 1) return e;
        if (d > a && at(d - 1) == "]") return d;
        int symbols = 0;
        for (std::size_t k = a; k < d; ++k) symbols += at(k).size() == 1 && std::isupper(static_cast<unsigned char>(at(k)[0])) ? 1 : 0;
        if (e - d == 1 && symbols == 1) return d;
        if (e - d >= 2) return e - 1;
        return e;
    }

    void chem_formula(std::size_t a, std::size_t b, std::vector<ItemPtr>& out) {
        std::size_t i = a;
        while (i < b && (ascii_digit(at(i)) || at(i) == "/")) out.push_back(glyph(i++));
        std::size_t phase = b;
        for (const char* tag : { "(aq)", "(s)", "(l)", "(g)", "(sc)", "(cr)" }) {
            const std::size_t n = std::strlen(tag);
            if (b - i < n) continue;
            std::string tail;
            for (std::size_t k = b - n; k < b; ++k) tail += at(k);
            if (tail == tag) {
                phase = b - n;
                break;
            }
        }
        const std::size_t charge = chem_charge_start(i, phase);
        const std::size_t body_end = charge == k_none ? phase : charge;
        std::vector<ItemPtr> items;
        bool after_dot = false;
        while (i < body_end) {
            const std::string c = at(i);
            if (c == "_") {
                const std::size_t marker = i++;
                if (i >= body_end) {
                    items.push_back(glyph(marker));
                    break;
                }
                chem_attach(items, chem_script_operand(i, body_end, false), marker, false);
                continue;
            }
            if (ascii_digit(c) && c != "." && !items.empty() && !after_dot) {
                const Item& prev = *items.back();
                const bool can = (prev.kind == Kind::Glyph && (ascii_letter(prev.shown) || prev.shown == ")" || prev.shown == "]")) || prev.kind == Kind::Group || (prev.kind == Kind::Script && !prev.sub);
                if (can) {
                    chem_attach(items, chem_script_operand(i, body_end, false), k_none, false);
                    continue;
                }
            }
            if (c == "*" || c == "\xC2\xB7" || c == "\xE2\x80\xA2") {
                items.push_back(glyph(i++, "\xC2\xB7"));
                after_dot = true;
                continue;
            }
            if (ascii_letter(c) || c == "(" || c == "[" || c == ")" || c == "]") after_dot = false;
            items.push_back(glyph(i++));
        }
        if (charge != k_none) {
            std::size_t k = charge;
            if (at(k) == "^") {
                const std::size_t marker = k++;
                if (k >= phase) items.push_back(glyph(marker));
                else chem_attach(items, chem_script_operand(k, phase, true), marker, true);
                while (k < phase) items.push_back(glyph(k++));
            } else {
                std::vector<ItemPtr> run;
                chem_glyphs(charge, phase, run);
                chem_attach(items, seq_of(std::move(run)), k_none, true);
            }
        }
        for (ItemPtr& it : items) out.push_back(std::move(it));
        for (std::size_t k = phase; k < b; ++k) out.push_back(glyph(k));
    }

    void chem_items(std::vector<ItemPtr>& out) {
        std::size_t i = 0;
        while (i < m_cps.size()) {
            if (chem_space(i)) {
                out.push_back(glyph(i++));
                continue;
            }
            std::size_t len = 0;
            std::string shown;
            if (chem_arrow(i, len, shown)) {
                std::vector<ItemPtr> parts;
                parts.push_back(glyph(i, shown));
                for (std::size_t k = 1; k < len; ++k) parts.push_back(glyph(i + k, std::string(), false));
                parts.front()->spaced = !(i > 0 && chem_space(i - 1)) || !chem_space(i + len);
                i += len;
                ItemPtr s = seq_of(std::move(parts));
                s->shown = "op";
                out.push_back(std::move(s));
                continue;
            }
            if (at(i) == "+") {
                ItemPtr g = glyph(i);
                g->spaced = !(i > 0 && chem_space(i - 1)) || !chem_space(i + 1);
                out.push_back(std::move(g));
                ++i;
                continue;
            }
            const std::size_t end = std::max(i + 1, chem_token_end(i));
            chem_formula(i, end, out);
            i = end;
        }
    }

    std::vector<CodePoint> m_cps;
    const MathInputMetrics& m_m;
    bool m_chem = false;
    std::vector<std::size_t> m_glyph_of;
    std::vector<Spot> m_caret_override;
    MathInputLayout m_out;
};

} // namespace

MathInputLayout layout_math_input(const std::string& text, const MathInputMetrics& metrics) {
    Builder b(text, metrics);
    return b.run();
}

MathInputLayout layout_formula_input(const std::string& text, const MathInputMetrics& metrics) {
    Builder b(text, metrics, true);
    return b.run();
}

const MathInputCaret& math_input_caret(const MathInputLayout& layout, std::size_t offset) {
    static const MathInputCaret none;
    if (layout.carets.empty()) return none;
    for (const MathInputCaret& c : layout.carets) {
        if (c.offset >= offset) return c;
    }
    return layout.carets.back();
}

std::size_t math_input_hit(const MathInputLayout& layout, float x, float y) {
    std::size_t best = 0;
    float best_d = std::numeric_limits<float>::max();
    const MathInputCaret* best_spot = nullptr;
    const float asc = layout.base_ascent;
    const float desc = layout.base_descent;
    for (const MathInputCaret& c : layout.carets) {
        const float top = c.y - asc * c.scale;
        const float bottom = c.y + desc * c.scale;
        const float dy = y < top ? top - y : (y > bottom ? y - bottom : 0.0f);
        const float d = std::fabs(x - c.x) + 3.0f * dy;
        const bool same_spot = best_spot && c.x == best_spot->x && c.y == best_spot->y && c.scale == best_spot->scale;
        if (d < best_d || (d == best_d && same_spot)) {
            best_d = d;
            best = c.offset;
            best_spot = &c;
        }
    }
    return best;
}

namespace {

bool same_spot(const MathInputCaret& a, const MathInputCaret& b) {
    return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f && std::fabs(a.scale - b.scale) < 0.001f;
}

std::size_t pick_in_run(const MathInputLayout& layout, std::size_t index, bool rightward) {
    std::size_t first = index;
    std::size_t last = index;
    while (first > 0 && same_spot(layout.carets[first - 1], layout.carets[index])) --first;
    while (last + 1 < layout.carets.size() && same_spot(layout.carets[last + 1], layout.carets[index])) ++last;
    if (rightward) {
        for (std::size_t k = last + 1; k-- > first;) {
            if (!layout.carets[k].weak) return layout.carets[k].offset;
        }
        return layout.carets[last].offset;
    }
    for (std::size_t k = last + 1; k-- > first;) {
        if (k > index) continue;
        if (!layout.carets[k].weak) return layout.carets[k].offset;
    }
    for (std::size_t k = first; k <= last; ++k) {
        if (!layout.carets[k].weak) return layout.carets[k].offset;
    }
    return layout.carets[index].offset;
}

std::size_t caret_index(const MathInputLayout& layout, std::size_t offset) {
    for (std::size_t k = 0; k < layout.carets.size(); ++k) {
        if (layout.carets[k].offset >= offset) return k;
    }
    return layout.carets.empty() ? 0 : layout.carets.size() - 1;
}

} // namespace

std::size_t math_input_left(const MathInputLayout& layout, std::size_t offset) {
    if (layout.carets.empty()) return 0;
    const std::size_t at = caret_index(layout, offset);
    const MathInputCaret& here = layout.carets[at];
    for (std::size_t k = at; k-- > 0;) {
        if (!same_spot(layout.carets[k], here)) return pick_in_run(layout, k, false);
    }
    return layout.carets.front().offset;
}

std::size_t math_input_right(const MathInputLayout& layout, std::size_t offset, std::size_t size) {
    if (layout.carets.empty()) return std::min(offset, size);
    const std::size_t at = caret_index(layout, offset);
    const MathInputCaret& here = layout.carets[at];
    for (std::size_t k = at + 1; k < layout.carets.size(); ++k) {
        if (!same_spot(layout.carets[k], here)) return std::min(pick_in_run(layout, k, true), size);
    }
    return std::min(layout.carets.back().offset, size);
}

std::size_t math_input_vertical(const MathInputLayout& layout, std::size_t offset, int direction) {
    const MathInputCaret& here = math_input_caret(layout, offset);
    const MathInputRegion* inner = nullptr;
    bool in_upper = false;
    std::size_t inner_width = std::string::npos;
    for (const MathInputRegion& r : layout.regions) {
        const bool up = r.has_upper && offset >= r.upper_begin && offset <= r.upper_end;
        const bool low = r.has_lower && offset >= r.lower_begin && offset <= r.lower_end;
        if (!up && !low) continue;
        const std::size_t width = up ? r.upper_end - r.upper_begin : r.lower_end - r.lower_begin;
        if (!inner || width < inner_width) {
            inner = &r;
            in_upper = up;
            inner_width = width;
        }
    }
    auto nearest = [&](std::size_t a, std::size_t b) {
        std::size_t pick = a;
        float best = std::numeric_limits<float>::max();
        for (std::size_t k = 0; k < layout.carets.size(); ++k) {
            const MathInputCaret& c = layout.carets[k];
            if (c.offset < a || c.offset > b || c.weak) continue;
            const float d = std::fabs(c.x - here.x);
            if (d < best) {
                best = d;
                pick = c.offset;
            }
        }
        return pick;
    };
    if (inner) {
        if (inner->fraction) {
            if (in_upper && direction > 0) return nearest(inner->lower_begin, inner->lower_end);
            if (!in_upper && direction < 0) return nearest(inner->upper_begin, inner->upper_end);
        } else if ((in_upper && direction > 0) || (!in_upper && direction < 0)) {
            return inner->after;
        }
    }
    for (const MathInputRegion& r : layout.regions) {
        if (r.after != offset) continue;
        if (direction < 0 && r.has_upper) return r.upper_end;
        if (direction > 0 && r.has_lower) return r.lower_end;
    }
    return std::string::npos;
}

} // namespace grove
