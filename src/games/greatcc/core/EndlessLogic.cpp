#include "games/greatcc/core/EndlessLogic.h"

namespace choco::games::greatcc {

int endlessNextSpawnMs(int nowMs, unsigned draw, unsigned delay, unsigned variance, float delayModifier) {
  const unsigned random = variance == 0 ? 0 : draw % variance;
  float wait = static_cast<float>((random + delay) - (variance >> 1)) * (1.0f - delayModifier);
  if (wait <= 0.0f) wait = 0.0f;
  if (wait > 2.0e9f) wait = 2.0e9f;  // (a variance beyond the delay wraps the original's unsigned sum; shipped values never do)
  return nowMs + static_cast<int>(wait);
}

bool endlessThresholdReached(int level, float thresholdMinutes, unsigned elapsedMs) {
  return level % 10 > 0 && thresholdMinutes <= static_cast<float>(elapsedMs / 1000) / 60.0f;
}

bool endlessMoneyDecayed(int nowMs, int stampMs, unsigned decayMs) {
  return stampMs != 0 && decayMs < static_cast<unsigned>(nowMs - stampMs);
}

unsigned randomCustomerIndex(unsigned count, unsigned draw) { return count <= 1 ? 0 : draw % count; }

}  // namespace choco::games::greatcc
