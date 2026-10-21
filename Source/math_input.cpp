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

enum class Kind { Glyph, Seq, Group, Script, Fraction, Radical };

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
};

using ItemPtr = std::unique_ptr<Item>;

ItemPtr make(Kind k) {
    ItemPtr p = std::make_unique<Item>();
    p->kind = k;
    return p;
}

class Builder {
public:
    Builder(const std::string& text, const MathInputMetrics& m) : m_cps(split(text)), m_m(m) {
        m_glyph_of.assign(m_cps.size(), k_none);
    }

    MathInputLayout run() {
        std::size_t i = 0;
        ItemPtr top = make(Kind::Seq);
        while (i < m_cps.size()) {
            std::vector<ItemPtr> part = parse_seq(i, "");
            for (ItemPtr& p : part) top->items.push_back(std::move(p));
            if (i < m_cps.size()) top->items.push_back(glyph(i++));
        }
        fraction_pass(top->items);
        spacing_pass(top->items);
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
            const MathInputGlyph& g = m_out.glyphs[m_glyph_of[k]];
            m_out.carets.push_back(MathInputCaret{ m_cps[k].begin, g.x, g.y, g.scale });
        }
        if (m_cps.empty()) {
            m_out.carets.push_back(MathInputCaret{ 0, 0.0f, 0.0f, 1.0f });
        } else {
            const MathInputGlyph& g = m_out.glyphs[m_glyph_of.back()];
            m_out.carets.push_back(MathInputCaret{ m_cps.back().end, g.x + g.width, g.y, g.scale });
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
            g->hide = true;
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
            while (start > 0) {
                const std::string* t = glyph_text(*items[start - 1]);
                if (t && (fraction_boundary(*t) || *t == "\xC2\xB7" || *t == ", ")) break;
                --start;
            }
            if (start == k) continue;
            ItemPtr f = make(Kind::Fraction);
            f->slash = it.cp;
            std::vector<ItemPtr> num;
            for (std::size_t q = start; q < k; ++q) num.push_back(std::move(items[q]));
            if (num.size() == 1 && num[0]->kind == Kind::Group && !num[0]->hide && m_cps[num[0]->open].text == "(" && num[0]->close != k_none) num[0]->hide = true;
            f->num = seq_of(std::move(num));
            ItemPtr den = std::move(items[k + 1]);
            if (den->kind == Kind::Group && !den->hide && m_cps[den->open].text == "(" && den->close != k_none) den->hide = true;
            f->den = std::move(den);
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
                if (it.inner->items.empty() && it.hide) b.w = std::max(b.w, 0.35f * em(s));
                break;
            }
            case Kind::Script: {
                const Box base = measure(*it.base, s);
                const float ss = std::max(0.42f, s * 0.7f);
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
                if (it.inner->items.empty() && it.hide) cx = x + it.box.w;
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
            case Kind::Script: {
                place(*it.base, x, y, s);
                const float sx = x + it.base->box.w + 0.02f * em(s);
                const float ss = std::max(0.42f, s * 0.7f);
                if (it.sup) {
                    put(it.sup_marker, "^", false, false, sx, y - it.sup_up, ss, 0.0f, 0.0f);
                    place(*it.sup, sx, y - it.sup_up, ss);
                }
                if (it.sub) {
                    put(it.sub_marker, "_", false, false, sx, y + it.sub_down, ss, 0.0f, 0.0f);
                    place(*it.sub, sx, y + it.sub_down, ss);
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
                if (it.close != k_none) put(it.close, ")", false, false, x + it.box.w, y, s, 0.0f, 0.0f);
                break;
            }
        }
    }

    std::vector<CodePoint> m_cps;
    const MathInputMetrics& m_m;
    std::vector<std::size_t> m_glyph_of;
    MathInputLayout m_out;
};

} // namespace

MathInputLayout layout_math_input(const std::string& text, const MathInputMetrics& metrics) {
    Builder b(text, metrics);
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
    const float asc = layout.base_ascent;
    const float desc = layout.base_descent;
    for (const MathInputCaret& c : layout.carets) {
        const float top = c.y - asc * c.scale;
        const float bottom = c.y + desc * c.scale;
        const float dy = y < top ? top - y : (y > bottom ? y - bottom : 0.0f);
        const float d = std::fabs(x - c.x) + 3.0f * dy;
        if (d < best_d) {
            best_d = d;
            best = c.offset;
        }
    }
    return best;
}

} // namespace grove
