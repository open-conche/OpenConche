#include "games/choc3/PlayerSave.h"

#include <cctype>
#include <string>

namespace choco::game {

bool isUninitialisedPlayerSave(std::string_view data) {
  // Walk the serialiser's `return {k=v,...}` shape and collect the keys at nesting depth 1 (strings skipped, so braces and
  // commas inside them do not count). Only the top-level money/time/knownRecipes matter.
  int depth = 0;
  bool inString = false, money = false, time = false, emptyRecipes = false;
  for (size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    if (inString) {
      if (c == '\\') ++i;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') inString = true;
    else if (c == '{') ++depth;
    else if (c == '}') --depth;
    else if (depth == 1 && i > 0 && (data[i - 1] == '{' || data[i - 1] == ',')) {
      size_t e = i;
      while (e < data.size() && (std::isalnum(static_cast<unsigned char>(data[e])) || data[e] == '_')) ++e;
      if (e == i || e >= data.size() || data[e] != '=') continue;
      const std::string_view key = data.substr(i, e - i);
      if (key == "money") money = true;
      else if (key == "time") time = true;
      else if (key == "knownRecipes") emptyRecipes = data.substr(e + 1, 2) == "{}";
      i = e;
    }
  }
  return emptyRecipes && !money && !time;
}

}  // namespace choco::game
