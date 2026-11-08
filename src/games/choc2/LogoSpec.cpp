#include "games/choc2/LogoSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace choco::game::choc2 {

namespace {

int clampInt(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

int number(const ui::Style& t, const char* key, int def) {
  if (auto v = t.number(key)) return static_cast<int>(std::lround(*v));
  return def;
}

void appendQuoted(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 32 || c == 127) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\%03d", c);  // a decimal escape, always three figures so a number can follow
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

}  // namespace

void LogoSpec::clamp() {
  for (int* p : {&lx, &ly, &la, &ls, &lr, &ta, &tch, &tcs, &tcv, &tx, &ty}) *p = clampInt(*p, 0, 1000);
  ts = clampInt(ts, kTextMin, kTextMax);
  lf = clampInt(lf, 0, static_cast<int>(logoFontNames().size()) - 1);
  for (int* p : {&tfo, &tfi, &tfb}) *p = *p > 0 ? 1 : 0;
  if (to != kLeft && to != kCenter && to != kRight) to = kCenter;
}

LogoSpec LogoSpec::fromStyle(const ui::Style& t, std::string_view name) {
  LogoSpec s;
  s.b = t.stringOr("b", s.b);
  s.l = t.stringOr("l", s.l);
  if (s.l.find('/') == std::string::npos) s.l = "logo/" + s.l;
  s.lx = number(t, "lx", s.lx);
  s.ly = number(t, "ly", s.ly);
  s.la = number(t, "la", s.la);
  s.ls = number(t, "ls", s.ls);
  s.lf = number(t, "lf", s.lf);
  s.lr = number(t, "lr", s.lr);
  s.t = t.stringOr("t", std::string(name));
  s.to = number(t, "to", s.to);
  s.ts = number(t, "ts", s.ts);
  s.ta = number(t, "ta", s.ta);
  s.tch = number(t, "tch", s.tch);
  s.tcs = number(t, "tcs", s.tcs);
  s.tcv = number(t, "tcv", s.tcv);
  s.tx = number(t, "tx", s.tx);
  s.ty = number(t, "ty", s.ty);
  s.tfo = number(t, "tfo", s.tfo);
  s.tfi = number(t, "tfi", s.tfi);
  s.tfb = number(t, "tfb", s.tfb);
  s.clamp();
  return s;
}

std::string LogoSpec::toLuaString() const {
  std::string out = "return {b=";
  appendQuoted(out, b);
  out += ",l=";
  appendQuoted(out, l);
  out += ",t=";
  appendQuoted(out, t);
  const std::pair<const char*, int> numbers[] = {{"lx", lx},   {"ly", ly},   {"la", la},   {"ls", ls},   {"lf", lf},
                                                  {"lr", lr},   {"to", to},   {"ts", ts},   {"ta", ta},   {"tch", tch},
                                                  {"tcs", tcs}, {"tcv", tcv}, {"tx", tx},   {"ty", ty},   {"tfo", tfo},
                                                  {"tfi", tfi}, {"tfb", tfb}};
  for (const auto& [key, value] : numbers) out += "," + std::string(key) + "=" + std::to_string(value);
  out += "}";
  return out;
}

const std::vector<std::string>& logoFontNames() {
  static const std::vector<std::string> names = {"baldur", "bolton",     "caliban",        "edda",       "fertigo",
                                                 "fontin", "humanistic", "noir-et-blanc", "romanuncial"};
  return names;
}

std::string logoBackgroundPath(const std::string& b) { return b.find('/') == std::string::npos ? "logo/" + b : b; }

std::vector<std::string> logoGraphicList(const std::vector<std::string>& itemsAllowed) {
  std::vector<std::string> out;
  for (int i = 1; i <= kLogoStockGraphics; ++i) out.push_back("logo/logo" + std::to_string(i));
  for (const std::string& item : itemsAllowed) out.push_back("item/" + item);
  return out;
}

float sliderForIndex(int i, int n) {
  return n <= 1 ? 0.0f : static_cast<float>(clampInt(i, 0, n - 1)) / static_cast<float>(n - 1);
}

int indexForSlider(float v, int n) {
  if (n <= 1) return 0;
  return clampInt(static_cast<int>(std::lround(static_cast<double>(v) * (n - 1))), 0, n - 1);
}

Rgb8 hsvToRgb(int h, int s, int v) {
  const double hue = std::fmod(static_cast<double>(clampInt(h, 0, 1000)) / 1000.0 * 6.0, 6.0);
  const double sat = clampInt(s, 0, 1000) / 1000.0, val = clampInt(v, 0, 1000) / 1000.0;
  const int sector = static_cast<int>(std::floor(hue));
  const double f = hue - sector;
  const double p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
  double r = val, g = t, b = p;
  switch (sector) {
    case 1: r = q, g = val, b = p; break;
    case 2: r = p, g = val, b = t; break;
    case 3: r = p, g = q, b = val; break;
    case 4: r = t, g = p, b = val; break;
    case 5: r = val, g = p, b = q; break;
    default: break;
  }
  return {static_cast<int>(std::lround(r * 255)), static_cast<int>(std::lround(g * 255)), static_cast<int>(std::lround(b * 255))};
}

void rgbToHsv(int r, int g, int b, int& h, int& s, int& v) {
  const double R = clampInt(r, 0, 255) / 255.0, G = clampInt(g, 0, 255) / 255.0, B = clampInt(b, 0, 255) / 255.0;
  const double mx = std::max({R, G, B}), mn = std::min({R, G, B}), d = mx - mn;
  double hue = 0;
  if (d > 0) {
    if (mx == R) hue = std::fmod((G - B) / d, 6.0);
    else if (mx == G) hue = (B - R) / d + 2;
    else hue = (R - G) / d + 4;
    if (hue < 0) hue += 6;
  }
  h = static_cast<int>(std::lround(hue / 6.0 * 1000)) % 1000;
  s = mx > 0 ? static_cast<int>(std::lround(d / mx * 1000)) : 0;
  v = static_cast<int>(std::lround(mx * 1000));
}

}  // namespace choco::game::choc2
