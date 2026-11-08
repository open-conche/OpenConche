#pragma once

#include <string>

struct lua_State;

namespace choco::script {

// Mod: both arguments clamped to [0, 4294967295] and truncated to unsigned, then a % b. The original divides by zero
// without a guard; here b == 0 gives 0 (and sets *divideByZero).
double modNative(double a, double b, bool* divideByZero = nullptr);

// Dollars: |x| < 0.5 is the literal "$0"; otherwise |x| rounded with "%.0f", `separator` between every three digits,
// `sign` in front, and a leading '-' for negatives.
std::string formatDollars(double amount, const std::string& moneySign, const std::string& separator);

// The money globals the locale sets in the original (values not determined; the US defaults apply when a global is
// missing or not a string).
struct MoneyFormat {
  std::string sign, separator;
};
MoneyFormat moneyFormat(lua_State* L);

// formatDollars with the current money globals. Leaves the Lua stack as it found it.
std::string dollars(lua_State* L, double amount);

}  // namespace choco::script
