#include "games/greatcc/core/Compare.h"

#include <cctype>
#include <string>

namespace choco::games::greatcc {

unsigned parseComparison(std::string_view text) {
  std::string s;
  for (char c : text)
    if (c != ' ') s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  unsigned mask = 0;
  std::size_t pos = 0;
  while (pos <= s.size()) {
    std::size_t plus = s.find('+', pos);
    if (plus == std::string::npos) plus = s.size();
    const std::string token = s.substr(pos, plus - pos);
    if (token == "not") mask |= kCmpNot;
    else if (token == "lessthan") mask |= kCmpLess;
    else if (token == "greaterthan") mask |= kCmpGreater;
    else if (token == "equal") mask |= kCmpEqual;
    pos = plus + 1;
  }
  for (char c : s) {
    if (c == '>') mask |= kCmpGreater;
    else if (c == '<') mask |= kCmpLess;
    else if (c == '=') mask |= kCmpEqual;
    else if (c == '!') mask |= kCmpNot;
  }
  return mask;
}

bool compareValue(double a, double b, unsigned mask) {
  // CSUtil::CompareValue: both sides are floats there, and "less and greater without equal" is false before `not` applies.
  const float x = static_cast<float>(a), y = static_cast<float>(b);
  const bool less = (mask & kCmpLess) != 0, greater = (mask & kCmpGreater) != 0, equal = (mask & kCmpEqual) != 0;
  bool r = false;
  if (less) {
    if (greater && !equal) return false;
    r = x < y;
  } else if (greater) {
    r = y < x;
  }
  if (equal) r = r || x == y;
  if (mask & kCmpNot) r = !r;
  return r;
}

}  // namespace choco::games::greatcc
