#pragma once

#include <string>

#include "games/choc2/Choc2Music.h"
#include "games/common/MoneyCounter.h"
#include "games/common/World.h"

namespace choco::game::choc2 {

// What Chocolatier 2's UI must remember across the windows that come and go. Every port or map swap builds a new Ledger, but
// the player's tab choice has to survive that. Owned by the context's Choc2World, which Choc2Module::attach makes, and it
// outlives the World's ledgers (see SessionHolder).
struct Session {
  // The ledger tab last passed to SetLedgerContents ("messages", "factories" or "inventory"); empty until one was. It is never reset when the player changes (INFERRED: a new user's
  // first ledger opens on the previous user's last tab until SetLedgerContents runs, as the main menu does at game start).
  // INFERRED (design 2.3): the original keeps this inside the native, so a new Ledger opens on the tab that was last shown.
  std::string ledgerTab;
  // The quest bar's Y, shared by every ledger (TLedger's static questY). 800 is the "unset" value ResetLedger leaves; the next
  // ledger built takes hideQuestY, and the bar slides up from there when there is a hint (docs/re/choc2-ledger-binary.md 2).
  static constexpr int kQuestUnset = 800;
  int questY = kQuestUnset;
  // The rest of TLedger's class-level statics (docs/re/choc2-ledger-binary.md 12.2): one money counter, hint and mode for every live
  // Ledger, which are only views of it. Unlike the original (every ledger steps the shared money, so N ledgers count N times
  // faster) it is stepped once per tick: `lastTickMs` is the manager time of the last step.
  game::MoneyCounter money;
  std::string questHint;  // "" = no hint, so the bar hides
  std::string mode;       // "port" or "map" as last passed to SetLedgerMode; "" until one was
  double lastTickMs = -1;
};

// Holds the Session as a base declared before World, so it is destroyed after World's ledgers (bases go in reverse
// declaration order, and a derived class's own members would go before the World base). The ledgers are only views of
// the session, so it must still exist while they are torn down.
struct SessionHolder {
  Session session;
};

// Chocolatier 2's game state: the World plus the Session, one object with one owner.
class Choc2World : public SessionHolder, public World {
 public:
  using World::World;
  Choc2Music music;  // SetMusicState's state machine
  void afterAudio(audio::Audio& a) override { music.tick(a); }
};

// The context's Chocolatier 2 session, or null for the other games (a dynamic_cast per call; do not cache the result).
inline Session* sessionOf(script::ScriptContext& ctx) {
  auto* w = dynamic_cast<Choc2World*>(ctx.gameState.get());
  return w ? &w->session : nullptr;
}
inline const Session* sessionOf(const script::ScriptContext& ctx) {
  auto* w = dynamic_cast<const Choc2World*>(ctx.gameState.get());
  return w ? &w->session : nullptr;
}

}  // namespace choco::game::choc2
