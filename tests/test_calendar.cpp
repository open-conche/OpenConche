// The calendar: Chocolatier 3's date arithmetic (games/common/Calendar) and the Date / DayOfYear natives, which both
// choc3 and the games that forward to it (choc2) must bind.
#include <doctest/doctest.h>

#include <string>

#include "ScriptHarness.h"
#include "games/common/Calendar.h"
#include "games/Registry.h"

using namespace choco;

namespace {

struct CalendarEnv : test::ScriptEnv {
  explicit CalendarEnv(const char* game) : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/host/poll"), host(ctx) {
    ctx.module = games::findModule(game);
    ctx.module->attach(ctx);
    booted = host.boot();
    ctx.strings.loadRows({{"date_format", "%1% %2%, %3%"}, {"month_1", "Jan"}, {"month_2", "Feb"}, {"month_3", "Mar"},
                          {"month_4", "Apr"}, {"month_5", "May"}, {"month_6", "Jun"}, {"month_7", "Jul"},
                          {"month_8", "Aug"}, {"month_9", "Sep"}, {"month_10", "Oct"}, {"month_11", "Nov"},
                          {"month_12", "Dec"}});
  }
  script::ScriptHost host;
  bool booted = false;
};

void checkNatives(const char* game) {
  CalendarEnv e(game);
  REQUIRE(e.booted);
  REQUIRE(e.run("a = Date(100); b = Date(0.5); c = DayOfYear(53)") == 0);
  CHECK(e.getString("a") == "May 22, 1948");
  CHECK(e.getString("b") == "Jun 23, 1946");
  CHECK(e.getNumber("c") == 6);
  CHECK(e.getString("a") == game::dateText(e.ctx.strings, 100));
  CHECK(e.getString("b") == game::dateText(e.ctx.strings, 0.5));
  CHECK(e.getNumber("c") == game::dayOfYear(53));
  CHECK(e.stubs.count("Date") == 0);
  CHECK(e.stubs.count("DayOfYear") == 0);
}

}  // namespace

TEST_CASE("calendar natives: Date and DayOfYear are bound in choc3, choc2 and choc1 with the same results") {
  SUBCASE("choc3") { checkNatives("choc3"); }
#ifdef CHOCO_GAME_CHOC2
  SUBCASE("choc2") { checkNatives("choc2"); }
#endif
#ifdef CHOCO_GAME_CHOC1
  SUBCASE("choc1") { checkNatives("choc1"); }
#endif
}
