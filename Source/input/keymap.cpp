#include "grove/input/keymap.hpp"
#include "grove/markup/events.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace grove {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view text) {
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return std::string(text.substr(a, b - a));
}

struct Alias {
    const char* name;
    const char* key;
};

constexpr Alias k_aliases[] = {
    { "leftarrow", "Left" }, { "rightarrow", "Right" }, { "uparrow", "Up" }, { "downarrow", "Down" },
    { "arrowleft", "Left" }, { "arrowright", "Right" }, { "arrowup", "Up" }, { "arrowdown", "Down" },
    { "left", "Left" }, { "right", "Right" }, { "up", "Up" }, { "down", "Down" },
    { "minus", "-" }, { "equals", "=" }, { "equal", "=" }, { "plus", "=" }, { "leftbracket", "[" }, { "rightbracket", "]" },
    { "backslash", "\\" }, { "semicolon", ";" }, { "quote", "'" }, { "apostrophe", "'" }, { "tilde", "`" }, { "grave", "`" },
    { "backquote", "`" }, { "comma", "," }, { "period", "." }, { "dot", "." }, { "slash", "/" },
    { "return", "Enter" }, { "enter", "Enter" }, { "numpadenter", "Enter" }, { "esc", "Escape" }, { "escape", "Escape" },
    { "del", "Delete" }, { "delete", "Delete" }, { "ins", "Insert" }, { "insert", "Insert" }, { "backspace", "Backspace" },
    { "tab", "Tab" }, { "space", "Space" }, { "spacebar", "Space" }, { "home", "Home" }, { "end", "End" },
    { "pageup", "PageUp" }, { "pgup", "PageUp" }, { "pagedown", "PageDown" }, { "pgdn", "PageDown" },
    { "click", "Click" }, { "leftclick", "Click" }, { "doubleclick", "DoubleClick" }, { "middleclick", "MiddleClick" }, { "rightclick", "RightClick" },
};

Chord chord(const char* key, bool ctrl = false, bool shift = false, bool alt = false) {
    Chord c;
    c.key = key;
    c.ctrl = ctrl;
    c.shift = shift;
    c.alt = alt;
    return c;
}

} // namespace

bool Chord::mouse() const noexcept {
    return key == "Click" || key == "DoubleClick" || key == "MiddleClick" || key == "RightClick";
}

bool Chord::operator<(const Chord& o) const noexcept {
    return std::tie(key, ctrl, shift, alt) < std::tie(o.key, o.ctrl, o.shift, o.alt);
}

std::string canonical_key_name(std::string_view raw) {
    const std::string name = trim(raw);
    if (name.empty()) return std::string();
    if (name.size() == 1) {
        const char c = name[0];
        if (std::isalpha(static_cast<unsigned char>(c))) return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if (std::isdigit(static_cast<unsigned char>(c))) return name;
        switch (c) {
        case '-': case '_': return "-";
        case '=': case '+': return "=";
        case '[': case '{': return "[";
        case ']': case '}': return "]";
        case '\\': case '|': return "\\";
        case ';': case ':': return ";";
        case '\'': case '"': return "'";
        case '`': case '~': return "`";
        case ',': case '<': return ",";
        case '.': case '>': return ".";
        case '/': case '?': return "/";
        default: return std::string();
        }
    }
    const std::string l = lower(name);
    for (const Alias& a : k_aliases) {
        if (l == a.name) return a.key;
    }
    if (l.size() >= 2 && l[0] == 'f' && std::all_of(l.begin() + 1, l.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        const int n = std::stoi(l.substr(1));
        if (n >= 1 && n <= 24) return "F" + std::to_string(n);
    }
    if (l.size() == 4 && l.rfind("num", 0) == 0 && std::isdigit(static_cast<unsigned char>(l[3]))) return std::string(1, l[3]);
    if (l.rfind("numpad", 0) == 0 && l.size() > 6) {
        std::string rest = name.substr(6);
        if (!rest.empty()) rest[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(rest[0])));
        return "Numpad" + rest;
    }
    return std::string();
}

bool is_modifier_key(std::string_view key_name) {
    const std::string l = lower(key_name);
    for (const char* m : { "shift", "leftshift", "rightshift", "control", "ctrl", "leftcontrol", "rightcontrol", "alt", "leftalt", "rightalt", "meta", "leftmeta", "rightmeta", "super", "capslock", "numlock", "altgr" }) {
        if (l == m) return true;
    }
    return false;
}

std::optional<Chord> parse_chord(std::string_view raw) {
    std::string text = trim(raw);
    if (text.empty()) return std::nullopt;
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+' && !current.empty()) {
            parts.push_back(trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) parts.push_back(trim(current));
    if (parts.empty()) return std::nullopt;
    Chord out;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string m = lower(parts[i]);
        if (m == "ctrl" || m == "control" || m == "cmd" || m == "command" || m == "meta" || m == "super") out.ctrl = true;
        else if (m == "shift") out.shift = true;
        else if (m == "alt" || m == "option" || m == "opt") out.alt = true;
        else return std::nullopt;
    }
    out.key = canonical_key_name(parts.back());
    if (out.key.empty()) return std::nullopt;
    return out;
}

std::vector<Chord> parse_chords(std::string_view text) {
    std::vector<Chord> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        while (end != std::string_view::npos && end > start && text[end - 1] == '+') end = text.find(',', end + 1);
        const std::string_view part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (const auto c = parse_chord(part)) {
            if (std::find(out.begin(), out.end(), *c) == out.end()) out.push_back(*c);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

std::string chord_text(const Chord& c) {
    if (c.key.empty()) return std::string();
    std::string out;
    if (c.ctrl) out += "Ctrl+";
    if (c.shift) out += "Shift+";
    if (c.alt) out += "Alt+";
    return out + c.key;
}

std::string chords_text(const std::vector<Chord>& chords) {
    std::string out;
    for (const Chord& c : chords) {
        if (!out.empty()) out += ", ";
        out += chord_text(c);
    }
    return out;
}

Chord chord_of(std::string_view key_name, unsigned modifiers) {
    Chord c;
    c.key = canonical_key_name(key_name);
    c.ctrl = (modifiers & (ModifierControl | ModifierMeta)) != 0;
    c.shift = (modifiers & ModifierShift) != 0;
    c.alt = (modifiers & ModifierAlt) != 0;
    return c;
}

const std::vector<KeyAction>& code_editor_actions() {
    static const std::vector<KeyAction> actions = {
        { "select-all", "Select all", "Select the whole file", { chord("A", true) }, false },
        { "copy", "Copy", "Copy the selection, or the line when nothing is selected", { chord("C", true) }, false },
        { "cut", "Cut", "Cut the selection, or the line when nothing is selected", { chord("X", true) }, false },
        { "paste", "Paste", "Paste; with several carets, one line at each", { chord("V", true) }, false },
        { "undo", "Undo", "Undo the last change", { chord("Z", true) }, false },
        { "redo", "Redo", "Redo what was undone", { chord("Y", true), chord("Z", true, true) }, false },
        { "indent", "Indent", "Indent the line, or every selected line", { chord("Tab") }, false },
        { "outdent", "Outdent", "Outdent the line, or every selected line", { chord("Tab", false, true) }, false },
        { "delete-word-left", "Delete word left", "Delete back to the start of the word", { chord("Backspace", true) }, false },
        { "delete-word-right", "Delete word right", "Delete forward to the end of the word", { chord("Delete", true) }, false },
        { "word-left", "Word left", "Move the caret back a word", { chord("Left", true) }, false },
        { "word-right", "Word right", "Move the caret forward a word", { chord("Right", true) }, false },
        { "select-word-left", "Select word left", "Extend the selection back a word", { chord("Left", true, true) }, false },
        { "select-word-right", "Select word right", "Extend the selection forward a word", { chord("Right", true, true) }, false },
        { "file-start", "Start of file", "Move the caret to the first line", { chord("Home", true) }, false },
        { "file-end", "End of file", "Move the caret to the last line", { chord("End", true) }, false },
        { "scroll-up", "Scroll up", "Scroll a line up without moving the caret", { chord("Up", true) }, false },
        { "scroll-down", "Scroll down", "Scroll a line down without moving the caret", { chord("Down", true) }, false },
        { "caret-above", "Add caret above", "With nothing selected, add a caret on the line above", { chord("Up", false, true) }, false },
        { "caret-below", "Add caret below", "With nothing selected, add a caret on the line below", { chord("Down", false, true) }, false },
        { "single-caret", "Back to one caret", "Drop the extra carets, or the selection", { chord("Escape") }, false },
        { "go-to-definition", "Go to definition", "Jump to where the clicked name is defined", { chord("Click", true) }, true },
        { "add-caret", "Add or remove a caret", "Put another caret where you click, or take one away", { chord("Click", false, false, true) }, true },
        { "column-select", "Column of carets", "Click straight above or below the caret to put a caret on every line between", { chord("Click", false, true) }, true },
        { "box-select", "Box selection", "Drag to select a rectangle: the same columns on every line from where you start to where you let go (a straight line up or down gives a caret on each line)", { chord("Click", true, false, true) }, true },
    };
    return actions;
}

const KeyAction* find_code_editor_action(std::string_view id) {
    for (const KeyAction& a : code_editor_actions()) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

Keymap parse_keymap(std::string_view text) {
    Keymap out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(';', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string entry = trim(text.substr(start, end - start));
        start = end + 1;
        const std::size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string id = trim(std::string_view(entry).substr(0, eq));
        const std::string value = trim(std::string_view(entry).substr(eq + 1));
        if (lower(value) == "none") out[id] = {};
        else out[id] = parse_chords(value);
    }
    return out;
}

std::string keymap_text(const Keymap& keymap) {
    std::string out;
    for (const auto& [id, chords] : keymap) {
        if (!out.empty()) out += "; ";
        out += id + "=" + (chords.empty() ? std::string("none") : chords_text(chords));
    }
    return out;
}

std::vector<Chord> effective_chords(const KeyAction& action, const Keymap& overrides) {
    const auto it = overrides.find(action.id);
    return it == overrides.end() ? action.defaults : it->second;
}

std::optional<std::string> code_action_for(const Keymap& overrides, const Chord& pressed) {
    for (const KeyAction& a : code_editor_actions()) {
        for (const Chord& c : effective_chords(a, overrides)) {
            if (c == pressed) return a.id;
        }
    }
    return std::nullopt;
}

bool is_code_default_chord(const Chord& pressed) {
    for (const KeyAction& a : code_editor_actions()) {
        for (const Chord& c : a.defaults) {
            if (c == pressed) return true;
        }
    }
    return false;
}

} // namespace grove
