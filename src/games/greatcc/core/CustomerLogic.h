#pragma once

// The arithmetic of the shop's customers and scoring (docs/re/greatcc-shop-simulation.md 5.2-5.5, 7.1, 7.3), with no sprites
// and no Lua: the state numbers, the patience drain and the mood bands, the pile of dollars a price makes, the sale and the tip,
// and the chain the combo bonus is made of. The customer sprite and the shop window keep one of these and delegate to it.

namespace choco::games::greatcc {

// VCustomer's +0x2ac.
enum CustomerStateId {
  kCustomerPrototype = 0,  // not in the shop
  kCustomerArrived = 1,    // fading in, the menu bubble over its head, waiting for the player
  kCustomerReading = 2,    // the player came: reading the menu (suspended)
  kCustomerOrdering = 3,   // the order is shown, waiting for the items
  kCustomerServed = 4,     // served completely
  kCustomerAngry = 5,      // patience gone
  kCustomerMoney = 6,      // a pile of money on the counter, waiting to be collected
  kCustomerDone = 7,       // ready to be removed
};

// eCustomerMood (0 until the first UpdateMood).
enum CustomerMoodId { kMoodNone = 0, kMoodHappy = 1, kMoodContent = 2, kMoodNeutral = 3, kMoodAnnoyed = 4, kMoodAngry = 5 };

// UpdateMood's bands on the patience: above 80 happy, above 60 content, above 40 neutral, above 20 annoyed, above 0 angry; 0 for
// nothing left (a patience that is not a number counts as nothing left, as every comparison of the original is false for it).
int moodForPatience(float patience);

// The table's `decay`: the milliseconds a fresh customer (100 patience) takes to lose it all when it is 1 or more, else the rate per
// millisecond itself. A value that is not a number gives a rate that is not a number.
float decayRateFromTable(float tableDecay);

// One drain: `patience - elapsedMs * rate * patienceModifier * levelDecay`, a patience below 0 raised to 0. The product is made in
// single precision, a factor at a time.
float drainPatience(float patience, unsigned elapsedMs, float rate, float patienceModifier, float levelDecay);

// How many extra `dollar` sprites a pile has: `floor(price / 8)` below a price of 48, else 5. (A price of 0.9 gives none.)
int payoutDollarCount(float price);

// The sale and the tip of ShopWindow::OrderComplete, before `salemodifier` and `tipmodifier` multiply them: the tip's base is the
// customer's patience as a fraction of 100 times its `tip`.
float tipBase(float patience, float tip);

// ShopWindow::AddCombo's chain: `lastType` and `counter`, both 0 at a level's start. The first event of a type only starts the chain.
struct ComboChain {
  int lastType = 0;
  int counter = 0;
  // A different type restarts the chain and gives no bonus (false); the same type counts up and gives one (true, `counter` now
  // holds how many have followed the first).
  bool add(int type) {
    if (type != lastType) {
      lastType = type;
      counter = 0;
      return false;
    }
    ++counter;
    return true;
  }
  void reset() { lastType = counter = 0; }
};

// eComboType: 1 the customer reached the menu, 2 an item was served, 4 money was collected.
enum ComboType { kComboMenu = 1, kComboService = 2, kComboPayment = 4 };
// The value that names a type's bonus per chain step (`menu_combobonus`, `service_combobonus`, `payout_combobonus`; any other type
// uses the menu's) and the string-table key of its label (`combo_menu`, `combo_service`, `combo_payment`).
const char* comboBonusValue(int type);
const char* comboLabelKey(int type);
// The value the longest chain of a type is kept in (`menu_multiplier_this_level`, ...); null for a type that keeps none.
const char* comboMultiplierValue(int type);
// The game state the points of a type's bonuses add up in (`MenuDeliveryChainPointsEarned`, ...); null for a type with none.
const char* comboPointsState(int type);

}  // namespace choco::games::greatcc
