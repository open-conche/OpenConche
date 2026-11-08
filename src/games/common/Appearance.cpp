#include "games/common/Appearance.h"

namespace choco::game {

namespace {

void readList(const ui::Style& list, std::vector<AppearanceLayer>& out) {
  const int n = list.size();
  for (int i = 1; i <= n; ++i) {
    if (auto s = list.stringAt(i)) {
      out.push_back({"custom/" + *s, {1, 1, 1, 1}});
    } else if (auto e = list.element(i)) {
      if (auto name = e->stringAt(1)) {
        auto num = [&](int k) { return static_cast<float>(e->numberAt(k).value_or(0)); };
        out.push_back({"custom/" + *name, {num(2), num(3), num(4), 1}});
      }
    }
  }
}

}  // namespace

std::vector<AppearanceLayer> loadAppearance(const ui::Style& t) {
  std::vector<AppearanceLayer> out;
  if (auto list = t.table("appearance")) {
    readList(*list, out);
  } else if (auto code = t.string("code"); code && !code->empty()) {
    out.push_back({"items/" + *code + "_big", {1, 1, 1, 1}});
  } else if (auto name = t.string("name"); name && !name->empty()) {
    out.push_back({"items/" + *name + "_big", {1, 1, 1, 1}});
  } else {
    readList(t, out);
  }
  return out;
}

void buildItemAppearance(Sprite& s, const std::vector<AppearanceLayer>& layers, float scale, Textures& textures) {
  while (!s.children().empty()) s.removeChild(*s.children().front());
  s.scale = scale;
  int layer = 0;
  for (const AppearanceLayer& l : layers) {
    if (auto tex = textures.texture(l.image)) {
      auto c = std::make_unique<Sprite>(layer++, "layer");
      c->texture = tex;
      c->tint = l.tint;
      s.addChild(std::move(c));
    }
    if (auto hi = textures.optionalTexture(l.image + "_highlight")) {
      auto c = std::make_unique<Sprite>(layer++, "highlight");
      c->texture = hi;
      s.addChild(std::move(c));
    }
  }
}

}  // namespace choco::game
