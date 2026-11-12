#include "games/greatcc/debug/DebugCommand.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace choco::games::greatcc {

DebugCommand parseDebugCommand(const std::string& line) {
  DebugCommand out;
  const std::size_t space = line.find(' ');
  out.word = line.substr(0, space);
  std::transform(out.word.begin(), out.word.end(), out.word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (space == std::string::npos) return out;
  std::size_t at = space + 1;
  while (at <= line.size()) {
    const std::size_t next = line.find(' ', at);
    const std::string piece = line.substr(at, next == std::string::npos ? std::string::npos : next - at);
    if (!piece.empty()) out.args.push_back(piece);
    if (next == std::string::npos) break;
    at = next + 1;
  }
  return out;
}

int debugToInt(const std::string& s) {
  const long v = std::strtol(s.c_str(), nullptr, 10);  // saturates at LONG_MIN / LONG_MAX
  return static_cast<int>(std::clamp<long>(v, INT_MIN, INT_MAX));
}

float debugToFloat(const std::string& s) {
  const double v = std::strtod(s.c_str(), nullptr);
  if (!std::isfinite(v)) return 0;
  return static_cast<float>(std::clamp(v, -3.0e38, 3.0e38));
}

int debugAddMinutes(int elapsedMs, int minutes) {
  const long long sum = static_cast<long long>(elapsedMs) + static_cast<long long>(minutes) * 60000LL;
  return static_cast<int>(std::clamp<long long>(sum, 0, INT_MAX));
}

int chocolateLayerTypeFromString(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (name.size() > 1 && name[0] == 'k') name.erase(0, 1);
  if (name.size() > 5 && name.compare(name.size() - 5, 5, "layer") == 0) name.erase(name.size() - 5);
  if (name == "bucket") return 0;
  if (name == "ingredient") return 1;
  if (name == "chocolate") return 2;
  if (name == "infusion") return 3;
  if (name == "topping") return 4;
  return -1;
}

}  // namespace choco::games::greatcc
