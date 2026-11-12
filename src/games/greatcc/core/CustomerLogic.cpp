#include "games/greatcc/core/CustomerLogic.h"

namespace choco::games::greatcc {

int moodForPatience(float p) {
  if (p > 80.0f) return kMoodHappy;
  if (p > 60.0f) return kMoodContent;
  if (p > 40.0f) return kMoodNeutral;
  if (p > 20.0f) return kMoodAnnoyed;
  if (p > 0.0f) return kMoodAngry;
  return kMoodNone;
}

float decayRateFromTable(float v) {
  if (v >= 1.0f || v != v) return 100.0f / v;
  return v;
}

float drainPatience(float patience, unsigned elapsedMs, float rate, float patienceModifier, float levelDecay) {
  const float step = static_cast<float>(elapsedMs) * rate * patienceModifier * levelDecay;
  float p = patience - step;
  if (p <= 0.0f && p != 0.0f) p = 0.0f;
  return p;
}

int payoutDollarCount(float price) {
  const int whole = static_cast<int>(price);
  return whole < 48 ? (whole >> 3) : 5;
}

float tipBase(float patience, float tip) { return (patience / 100.0f) * tip; }

const char* comboBonusValue(int type) {
  if (type == kComboService) return "service_combobonus";
  if (type == kComboPayment) return "payout_combobonus";
  return "menu_combobonus";
}
const char* comboLabelKey(int type) {
  if (type == kComboService) return "combo_service";
  if (type == kComboPayment) return "combo_payment";
  return "combo_menu";
}
const char* comboMultiplierValue(int type) {
  if (type == kComboService) return "service_multiplier_this_level";
  if (type == kComboPayment) return "payment_multiplier_this_level";
  if (type == kComboMenu) return "menu_multiplier_this_level";
  return nullptr;
}
const char* comboPointsState(int type) {
  if (type == kComboService) return "ServiceChainPointsEarned";
  if (type == kComboPayment) return "CashPickupChainPointsEarned";
  if (type == kComboMenu) return "MenuDeliveryChainPointsEarned";
  return nullptr;
}

}  // namespace choco::games::greatcc
