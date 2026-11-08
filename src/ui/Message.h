#pragma once

#include <cstdint>
#include <string>

namespace choco::ui {

using WindowId = std::uint32_t;  // never reused; 0 means none

// Message types, as defined by the prelude's k* constants (each is the previous plus one).
inline constexpr int kGeneric = 0;
inline constexpr int kCloseWindow = 1;
inline constexpr int kDefaultAction = 2;
inline constexpr int kButtonPress = 3;
inline constexpr int kPressAnyKey = 4;
inline constexpr int kQuitNow = 5;
inline constexpr int kModalClosed = 6;
inline constexpr int kTextEditChanged = 7;
inline constexpr int kCommandOnly = 8;
inline constexpr int kSliderValChanged = 9;
inline constexpr int kSliderMouseUp = 10;
inline constexpr int kSliderPageUp = 11;
inline constexpr int kSliderPageDown = 12;

// The Lua side reads only `type` and `name`. `target` is a window id (0 = focus, else the top modal).
struct Message {
  int type = 0;
  std::string name;
  WindowId target = 0;
};

}  // namespace choco::ui
