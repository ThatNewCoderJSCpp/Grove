#ifndef GROVE_INPUT_KEYMAP_HPP
#define GROVE_INPUT_KEYMAP_HPP

#include "../markup/node.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grove {

struct Chord {
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool empty() const noexcept { return key.empty(); }
    bool mouse() const noexcept;
    bool operator==(const Chord& o) const noexcept { return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt; }
    bool operator!=(const Chord& o) const noexcept { return !(*this == o); }
    bool operator<(const Chord& o) const noexcept;
};

std::string canonical_key_name(std::string_view name);
std::optional<Chord> parse_chord(std::string_view text);
std::vector<Chord> parse_chords(std::string_view text);
std::string chord_text(const Chord& chord);
std::string chords_text(const std::vector<Chord>& chords);
Chord chord_of(std::string_view key_name, unsigned modifiers);
bool is_modifier_key(std::string_view key_name);

struct KeyAction {
    std::string id;
    std::string label;
    std::string help;
    std::vector<Chord> defaults;
    bool mouse = false;
};

const std::vector<KeyAction>& code_editor_actions();
const KeyAction* find_code_editor_action(std::string_view id);

using Keymap = std::map<std::string, std::vector<Chord>>;

Keymap parse_keymap(std::string_view text);
std::string keymap_text(const Keymap& keymap);
std::vector<Chord> effective_chords(const KeyAction& action, const Keymap& overrides);
std::optional<std::string> code_action_for(const Keymap& overrides, const Chord& pressed);
bool is_code_default_chord(const Chord& pressed);

} // namespace grove

#endif // GROVE_INPUT_KEYMAP_HPP
