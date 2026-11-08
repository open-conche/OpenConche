#include "script/Formats.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "script/LuaApi.h"

namespace choco::script {

namespace {

std::uint32_t toUnsigned(double v) {
  if (std::isnan(v) || v <= 0) return 0;
  if (v >= 4294967295.0) return 4294967295u;
  return static_cast<std::uint32_t>(v);
}

}  // namespace

double modNative(double a, double b, bool* divideByZero) {
  const std::uint32_t ub = toUnsigned(b);
  if (divideByZero) *divideByZero = ub == 0;
  if (ub == 0) return 0;
  return static_cast<double>(toUnsigned(a) % ub);
}

std::string formatDollars(double amount, const std::string& moneySign, const std::string& separator) {
  if (!(std::fabs(amount) >= 0.5)) return "$0";  // also catches NaN
  char buf[400];
  std::snprintf(buf, sizeof buf, "%.0f", std::fabs(amount));
  const std::string digits = buf;
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (digits.size() - i) % 3 == 0) out += separator;
    out += digits[i];
  }
  return std::string(amount < 0 ? "-" : "") + moneySign + out;
}

namespace {

std::string globalString(lua_State* L, const char* name, const char* def) {
  const int top = lua_gettop(L);
  lua_getglobal(L, name);
  std::string s = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : def;
  lua_settop(L, top);
  return s;
}

}  // namespace

MoneyFormat moneyFormat(lua_State* L) {
  return {globalString(L, "_MONEY_SIGN", "$"), globalString(L, "_NUMERIC_SEPARATOR", ",")};
}

std::string dollars(lua_State* L, double amount) {
  const MoneyFormat m = moneyFormat(L);
  return formatDollars(amount, m.sign, m.separator);
}

}  // namespace choco::script
