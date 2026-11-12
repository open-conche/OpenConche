#pragma once

// The arithmetic of the endless run (docs/re/greatcc-shop-simulation.md 2.5, 2.6): the delay to the next arrival, the thresholds
// that step the pseudo-level up, the money that decays and the draw of a random customer. Single precision where the original is.

namespace choco::games::greatcc {

// CHOC_VENDOR_UPDATE_INTERVAL: UpdateEndlessMode looks at the thresholds when more than this many timer milliseconds have passed.
constexpr unsigned kEndlessUpdateIntervalMs = 250;

// The first arrival of the run: the timer's elapsed milliseconds.
constexpr int kEndlessFirstSpawnMs = 3000;

// The timer's elapsed milliseconds the next arrival is due: `now + max(0, (draw % variance + delay - variance / 2) * (1 - modifier))`.
// `delay` and `variance` are the floored values; a variance of 0 gives no random part (the original would divide by it).
int endlessNextSpawnMs(int nowMs, unsigned draw, unsigned delay, unsigned variance, float delayModifier);

// UpdateEndlessMode's test: the pseudo-level `level` (1..9 of its location) steps up when its threshold (minutes) is reached by the
// timer, `(elapsedMs / 1000) / 60`. A level that is a multiple of ten has no threshold.
bool endlessThresholdReached(int level, float thresholdMinutes, unsigned elapsedMs);

// VCustomer::Update's decay of a pile of money: it goes when more than `decayMs` have passed since the stamp (a stamp of 0 is no pile).
bool endlessMoneyDecayed(int nowMs, int stampMs, unsigned decayMs);

// VCustomerVendor::GetRandomCustomer's index into the posse followed by the locals: 0 with one entry or fewer (no draw is made).
unsigned randomCustomerIndex(unsigned count, unsigned draw);

}  // namespace choco::games::greatcc
