#include "games/common/Calendar.h"

#include <cmath>
#include <cstdint>

namespace choco::game {

namespace {

std::uint32_t toUnsigned(double v) {
  if (std::isnan(v) || v <= 0) return 0;
  if (v >= 4294967295.0) return 4294967295u;
  return static_cast<std::uint32_t>(v);
}

}  // namespace

GameDate dateFromWeeks(double weeks) {
  const std::uint32_t days = toUnsigned(weeks * 7.0 + 171.0);
  const int yearOffset = static_cast<int>(days / 365);
  int dayInYear = static_cast<int>(days % 365);
  int length[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const auto uy = static_cast<std::uint32_t>(yearOffset);  // Gregorian, applied to the offset from 1946
  if (uy % 400 == 0 || (uy % 4 == 0 && uy % 100 != 0)) length[1] = 29;
  int month = 0;
  while (month < 11 && dayInYear >= length[month]) dayInYear -= length[month++];
  return {1946 + yearOffset, month + 1, dayInYear + 1};
}

int dayOfYear(double weeks) { return static_cast<int>(toUnsigned(weeks * 7.0) % 365); }

std::string dateText(const assets::StringTable& strings, double weeks) {
  const GameDate d = dateFromWeeks(weeks);
  return strings.sdkLookup("date_format",
                           {strings.sdkLookup("month_" + std::to_string(d.month)), std::to_string(d.day), std::to_string(d.year)});
}

}  // namespace choco::game
