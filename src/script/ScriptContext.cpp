#include "script/ScriptContext.h"

#include <algorithm>
#include <fstream>

#include "sdk/Users.h"
#include "script/GameModule.h"
#include "platform/PathUtil.h"

namespace choco::script {

void ScriptContext::saveFullscreenPref(bool fullscreen) {
  if (users) users->setGlobalPref("fullscreen", fullscreen ? "1" : "0");
}

ScriptContext::ScriptContext(LuaState& l, assets::AssetFS& f, ui::WindowManager& w, Clock& c, Rng& r, Config& cfg, StubLog& s, Log& lg)
    : lua(l), fs(f), wm(w), clock(c), rng(r), config(cfg), stubs(s), log(lg) {}

ScriptContext::~ScriptContext() = default;


int Rng::range(int lo, int hi) {
  if (hi < lo) return lo;
  double span = static_cast<double>(hi) - static_cast<double>(lo) + 1.0;
  int v = lo + static_cast<int>(span * uniform());
  return std::min(v, hi);
}

int Rng::fxRange(int lo, int hi) {
  if (hi < lo) return lo;
  const double span = static_cast<double>(hi) - static_cast<double>(lo) + 1.0;
  const int v = lo + static_cast<int>(span * unit(fx_));
  return std::min(v, hi);
}

int Rng::sideRange(int lo, int hi) {
  if (hi < lo) return lo;
  const double span = static_cast<double>(hi) - static_cast<double>(lo) + 1.0;
  const int v = lo + static_cast<int>(span * unit(side_));
  return std::min(v, hi);
}

std::string StubLog::render() const {
  std::vector<std::pair<std::string, int>> rows(counts_.begin(), counts_.end());
  std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string out;
  for (const auto& [name, n] : rows) out += name + "\t" + std::to_string(n) + "\n";
  return out;
}

bool StubLog::writeFile(const std::string& path) const {
  std::ofstream f(platform::pathFromUtf8(path), std::ios::binary);
  if (!f) return false;
  f << render();
  return static_cast<bool>(f);
}

}  // namespace choco::script
