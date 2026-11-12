#pragma once

// Synthetic `.anm` files for the Great Chocolate Chase tests: the version-2 layout (docs/re/greatcc-sprites-and-zones.md 1.1) built
// in memory, so no shipped sheet is copied into the tree.
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace greatcc_test {

constexpr float kNone = -10000000.0f;

struct Bytes {
  std::string b;
  void tag(const char* t) { b.append(t, 4); }
  void u32(std::uint32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); }
  void f32(float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    u32(v);
  }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    b += s;
  }
};

struct FrameSpec {
  int regX = 0, regY = 0, x = 0, y = 0, w = 10, h = 10;
  float scale = 1;
  bool hasXform = false;
  float xform[6] = {1, 0, 0, 1, 0, 0};
  std::vector<std::tuple<int, int, std::string>> anchors;
};

inline std::string makeAnm(const std::vector<FrameSpec>& frames, const std::string& script) {
  Bytes o;
  o.tag("ANIM");
  o.u32(1);
  o.u32(2);
  o.str("C:\\build\\x.xml");
  o.str(".\\*");
  o.tag("IMGN");
  for (std::uint32_t v : {0u, 0u, 0u, 0u}) o.u32(v);
  o.str("sheet.png");
  o.str("");
  o.tag("FRMS");
  o.u32(static_cast<std::uint32_t>(frames.size()));
  for (const FrameSpec& f : frames) {
    o.u32(static_cast<std::uint32_t>(f.regX));
    o.u32(static_cast<std::uint32_t>(f.regY));
    for (int v : {f.x, f.y, f.w, f.h}) o.u32(static_cast<std::uint32_t>(v));
    o.f32(f.scale);
    for (int k = 0; k < 6; ++k) o.f32(f.hasXform ? f.xform[k] : (k == 0 ? kNone : 0.0f));
    o.str("src.png");
    o.u32(static_cast<std::uint32_t>(f.anchors.size()));
    for (const auto& [ax, ay, an] : f.anchors) {
      o.u32(static_cast<std::uint32_t>(ax));
      o.u32(static_cast<std::uint32_t>(ay));
      o.str(an);
    }
  }
  if (!script.empty()) {
    o.tag("SCRP");
    o.str(script);
  }
  return o.b;
}


}  // namespace greatcc_test
