// The Great Chocolate Chase, slice 18 (docs/design/greatcc.md section 6): the arithmetic of the endless run. The numbers the
// difficulty scales are read through a ValueManager over the shipped declarations (typed in here).
#include <doctest/doctest.h>

#include "games/greatcc/core/EndlessLogic.h"
#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/ValueManager.h"

using namespace choco::games::greatcc;

namespace {
const char* kValues =
    "<values>"
    "<value name='endless_diff_modifier' value='0'/>"
    "<value name='endless_medium_modifier' value='-0.3'/>"
    "<value name='endless_hard_modifier' value='-0.5'/>"
    "<value name='customer_delay' value='15000' min='3000' max='15000'><modify byval='endless_diff_modifier'/></value>"
    "<value name='customer_delay_variance' value='6000'><modify byval='endless_diff_modifier'/></value>"
    "<value name='angry_customer_limit' value='10'><modify byval='endless_diff_modifier'/></value>"
    "<value name='endless_threshold_1' value='1'><modify byval='endless_diff_modifier'/></value>"
    "<value name='endless_threshold_9' value='15'><modify byval='endless_diff_modifier'/></value>"
    "<value name='customer_delay_modifier' value='0.0006' max='0.90'/>"
    "</values>";

struct Values {
  Progress progress;
  ValueManager v{progress};
  explicit Values(double modifier) {
    v.load(kValues);
    v.setValue("endless_diff_modifier", static_cast<float>(modifier), false);
  }
};
}  // namespace

TEST_CASE("endless difficulty: the modifier scales the delay, its variance, the angry limit and the thresholds") {
  Values easy(0), medium(-0.3), hard(-0.5);
  CHECK(easy.v.getUint("customer_delay", 0) == 15000);
  CHECK(medium.v.getUint("customer_delay", 0) == 10500);
  CHECK(hard.v.getUint("customer_delay", 0) == 7500);
  CHECK(easy.v.getUint("customer_delay_variance", 0) == 6000);
  CHECK(medium.v.getUint("customer_delay_variance", 0) == 4200);
  CHECK(hard.v.getUint("customer_delay_variance", 0) == 3000);
  CHECK(easy.v.getUint("angry_customer_limit", 0) == 10);
  CHECK(medium.v.getUint("angry_customer_limit", 0) == 7);
  CHECK(hard.v.getUint("angry_customer_limit", 0) == 5);
  CHECK(easy.v.getReal("endless_threshold_1", 0) == doctest::Approx(1.0));
  CHECK(hard.v.getReal("endless_threshold_9", 0) == doctest::Approx(7.5));
}

TEST_CASE("endlessNextSpawnMs: now plus (draw % variance + delay - variance / 2), shortened by the modifier, never before now") {
  CHECK(endlessNextSpawnMs(3000, 0, 15000, 6000, 0.0f) == 3000 + 12000);
  CHECK(endlessNextSpawnMs(3000, 5999, 15000, 6000, 0.0f) == 3000 + 17999);
  CHECK(endlessNextSpawnMs(3000, 6000, 15000, 6000, 0.0f) == 3000 + 12000);  // the draw wraps
  CHECK(endlessNextSpawnMs(0, 0, 15000, 6000, 0.5f) == 6000);
  CHECK(endlessNextSpawnMs(0, 0, 15000, 6000, 0.9f) == 1200);
  CHECK(endlessNextSpawnMs(100, 7, 0, 0, 0.0f) == 100);   // no variance: no draw to wrap, nothing to wait
  CHECK(endlessNextSpawnMs(100, 7, 1000, 0, 0.0f) == 1100);
  CHECK(endlessNextSpawnMs(100, 0, 100, 6000, 1.5f) == 100);  // a negative wait is none
}

TEST_CASE("customer_delay_modifier: 0.0006 a second, held at 0.90 after 25 minutes") {
  Values e(0);
  CHECK(e.v.getProduct("customer_delay_modifier", 0.0f, true) == doctest::Approx(0.0f));
  CHECK(e.v.getProduct("customer_delay_modifier", 600.0f, true) == doctest::Approx(0.36f));
  CHECK(e.v.getProduct("customer_delay_modifier", 1500.0f, true) == doctest::Approx(0.90f));
  CHECK(e.v.getProduct("customer_delay_modifier", 5000.0f, true) == doctest::Approx(0.90f));
}

TEST_CASE("endlessThresholdReached: the whole seconds in minutes against the threshold; a tenth level has none") {
  CHECK_FALSE(endlessThresholdReached(1, 1.0f, 59999));
  CHECK(endlessThresholdReached(1, 1.0f, 60000));
  CHECK(endlessThresholdReached(1, 1.0f, 60999));
  CHECK_FALSE(endlessThresholdReached(2, 3.0f, 179000));
  CHECK(endlessThresholdReached(2, 3.0f, 180000));
  CHECK_FALSE(endlessThresholdReached(10, 0.0f, 1000000));
  CHECK_FALSE(endlessThresholdReached(20, 0.0f, 1000000));
  CHECK(endlessThresholdReached(11, 1.0f, 60000));  // the level number's last digit picks the threshold
  // a harder mode scales the thresholds: 1 minute becomes 30 s at -0.5
  CHECK(endlessThresholdReached(1, 0.5f, 30000));
}

TEST_CASE("endlessMoneyDecayed: more than the decay time after the stamp; no stamp is no pile") {
  CHECK_FALSE(endlessMoneyDecayed(20000, 5000, 15000));  // exactly 15 s is not past it
  CHECK(endlessMoneyDecayed(20001, 5000, 15000));
  CHECK_FALSE(endlessMoneyDecayed(99999, 0, 15000));
}

TEST_CASE("randomCustomerIndex: uniform over the posse and the locals; one entry or none needs no draw") {
  CHECK(randomCustomerIndex(24, 0) == 0);
  CHECK(randomCustomerIndex(24, 25) == 1);
  CHECK(randomCustomerIndex(24, 0xffffffffu) == 0xffffffffu % 24);
  CHECK(randomCustomerIndex(1, 77) == 0);
  CHECK(randomCustomerIndex(0, 77) == 0);
}
