#include "games/choc1/Choc1World.h"

#include "games/choc1/Choc1Natives.h"
#include "games/choc1/Choc1Windows.h"
#include "script/ScriptContext.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc1 {

Choc1World* choc1WorldOf(script::ScriptContext& ctx) { return dynamic_cast<Choc1World*>(ctx.gameState.get()); }

void Choc1World::showMoney() {
  const std::string text = script::choc1Dollars(ctx_.strings, money.shown());
  script::forEachNamedInChain(ctx_.wm, "money", [&](ui::Window& w) {
    if (w.hasLabel()) w.setLabel(text);
  });
}

void Choc1World::afterAudio(audio::Audio& a) {
  AudioMusicSink sink(a);
  music.tick(sink, nowMs());
}

void Choc1World::tick(double now) {
  World::tick(now);
  if (money.tick(now)) showMoney();

  // The pulsers (TImagePulser): a 100 ms task each.
  for (std::size_t i = 0; i < pulses.size();) {
    Choc1Pulse& p = pulses[i];
    if (now < p.nextMs) {
      ++i;
      continue;
    }
    p.nextMs += Choc1Pulse::kPeriodMs;
    if (now >= p.nextMs) p.nextMs = now + Choc1Pulse::kPeriodMs;
    ui::BitmapTintWindow* tint = nullptr;
    script::forEachNamedInChain(ctx_.wm, p.name, [&](ui::Window& w) {
      if (!tint) tint = dynamic_cast<ui::BitmapTintWindow*>(&w);
    });
    if (tint && p.count > 0) {
      const float v = static_cast<float>(Choc1Pulse::kCycle[p.index % 10]);
      tint->setTint({v, v, v, 1});
      ++p.index;
      --p.count;
      ++i;
      continue;
    }
    if (tint) tint->setTint({1, 1, 1, 1});  // done (or the window is gone): the tint goes back to 1.0
    pulses.erase(pulses.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

}  // namespace choco::game::choc1
