#pragma once

#include <string>

namespace choco::ui {

// One keyboard event as the windows see it: the SDL key name ("A", "Return", "Escape", "F1", "Space") and modifiers.
struct KeyEvent {
  std::string name;
  bool shift = false, ctrl = false, alt = false;
};

// One input event from the platform layer, already in logical (800x600) coordinates. `inArea` is false when the
// pointer is in the letterbox bars. Only the left button (1; 0 counts too) acts as a click.
struct InputEvent {
  enum class Type { MouseMove, MouseDown, MouseUp, Key, Text } type = Type::MouseMove;
  float x = 0, y = 0;
  bool inArea = true;
  int button = 1;
  KeyEvent key;
  std::string text;  // Type::Text: the UTF-8 text the platform's text input produced
};

// NormalizeKeyString (docs/re/window-system.md section 4.3): lower-case; "@name" (longer than one character) is kept as
// is; otherwise "shift-", "ctrl-" (or "control-") and "alt-" found anywhere in the text become prefixes in that order,
// followed by the upper-cased first character of what remains. "alt-s" -> "alt-S", "x" -> "X".
std::string normalizeKeyString(const std::string& key);

// The hotkey string for a key event, before normalising: modifiers plus the character for a one-character key name,
// "@" plus the lower-cased name for named keys (@return, @escape, @f1), which keep no modifier prefix.
std::string hotkeyOf(const KeyEvent& ev, bool withModifiers);

// The character a key produces for the "raw:" name of kPressAnyKey and for Enter/Escape: the lower-cased one-character
// name, '\r' for Return and Keypad Enter, 27 for Escape, ' ' for Space, '\t' for Tab, 8 for Backspace; 0 otherwise.
char keyChar(const KeyEvent& ev);

}  // namespace choco::ui
