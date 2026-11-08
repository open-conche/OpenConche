#pragma once

#include <string>

#include "assets/Content.h"

namespace choco::game {

// Chocolatier 3's calendar. Game weeks to a calendar date: days = weeks * 7 + 171, year = 1946 + days / 365 (365-day
// years), then the month is found by walking the month lengths with February at 29 days when the year offset
// (days / 365) is a leap year.
struct GameDate {
  int year = 1946, month = 1, day = 1;
};
GameDate dateFromWeeks(double weeks);

// DayOfYear: (unsigned)(weeks * 7) % 365; negative weeks give 0.
int dayOfYear(double weeks);

// The text of the Date native: the string "date_format" with the "month_N" name, the day and the year.
std::string dateText(const assets::StringTable& strings, double weeks);

}  // namespace choco::game
