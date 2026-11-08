# The dice game (`DiceGame`, Chocolatier 1 and 2) and the crates game (`BonusGame`, Chocolatier 2)

Scope: the native `DiceGame{...}` window and its natives in Chocolatier 1 (`TDiceGame`), the Chocolatier 2 Lua that drives a different `DiceGame` (two-stage roll, raise-the-bet), and Chocolatier 2's `BonusGame` against Chocolatier 3's `CratesGame`. The companion table is `docs/re/choc-dice-game.tsv`. The factory game is in `docs/re/choc-factory-game.md`.

Sources, in trust order: the Ghidra decompile of the unpacked Chocolatier 1 `chocolatier.exe` (`re/choc1/decomp/_unnamed.c`; `FUN_<address>` names; addresses are exe virtual addresses), the Chocolatier 1 and 2 Lua (bytecode disassembled with a workbench script), the Chocolatier 2 string table, and for the crates comparison the original assets' `ui/ui_crates.lua` (plain text) with `docs/re/game-windows-questions.md` section 6 and `docs/re/ledger-and-game-natives.md` 10.1. Chocolatier 2's exe is packed, so **no Chocolatier 2 native is decoded**; for it only the Lua contract is recorded and every native behaviour is an inference.

## 0. Summary

* Chocolatier 1's `DiceGame` is a **one-shot four-dice contest**. Lua calls `RollDice(wagerText)`; the native rolls four dice, animates them for 5.02 s (all spinning for 2 s, then one die stops every 750 ms, or one per press of `StopDie`), compares the two players' sums and pays (or charges) the wager by running a Lua statement string. Ties pay nothing.
* The native draws the final faces **first** (`rand() % 6` per die, in order), then re-draws every die's displayed face on every 62 ms tick; dice that have stopped show their fixed face.
* Chocolatier 2's `DiceGame` is the **same four-dice idea with a new flow**: eight dice on screen (four per side), a first roll of the player's first two dice, a chance to raise the bet, then a second roll of the rest; highest total wins; the ante is paid at the first roll and a tie loses the ante. The Lua is fully visible; the native is not.
* `BonusGame` is Chocolatier 3's `CratesGame` with 25 crates instead of 21: same bet limits, same flow, same natives (`PrepareGame`, `SelectPrize`, `SetCommand`), same string keys.

## 1. Chocolatier 1 `TDiceGame`

### 1.1 Where it lives (fact)
| Thing | Address |
|---|---|
| vtable | `004e6de4` |
| constructor | `00408fa0` |
| destructor | `00408670` |
| `Init` (slot 5) | `00408810` |
| tick task (slot 13) | `00408b40` |
| `RollDice` method / Lua thunk | `00408a30` / `00408f30` |
| `StopDie` method / Lua thunk | `004089e0` / `00408f80` |
| redraw of the faces | `00408910` |
| `UtilEnableWindow` / `UtilDisableWindow` helpers | `0041de40` / `0041ded0` (set / clear bit `0x100` of a named window's flags; clear = hidden and inert) |

The constructor registers two natives on the shared Lua state: **`RollDice(string)`** and **`StopDie()`**. The destructor sets both back to nothing (`0042c020`). It loads sounds `audio/sfx/dice_game_roll.ogg` (`+0x34`), `dice_game_win.ogg` (`+0x3c`), `dice_game_lose.ogg` (`+0x44`), `dice_game_tie.ogg` (`+0x4c`) and six pictures `image/die1..die6` (50x50 PNG) into 8-byte slots at `+0x70 + 8*k`, `k = 0..5` (so a face value `v` in `0..5` shows `image/die<v+1>`).

### 1.2 Object fields
| Offset | Meaning |
|---|---|
| `+0x34/+0x3c/+0x44/+0x4c` | sound handles: roll, win, lose, tie |
| `+0x54` | elapsed ms of the roll |
| `+0x58` | the wager, a string (as given to `RollDice`) |
| `+0x5c` | `N`, the number of dice found in `Init` |
| `+0x60 + 4*i` | final face of die `i+1` (0..5) |
| `+0x70 + 8*k` | the six die pictures |

### 1.3 `Init` (`00408810`)
Calls the window base Init, then repeatedly looks up a child named `die<N+1>` (`FUN_00421bf0`, name search below the DiceGame) and counts it if it exists and is a Bitmap, stopping at the first missing or non-Bitmap child. Having counted a die it also **re-images that child to picture slot 0** (the picture slots' `_SetBitmap`-image call `FUN_0042f860` with the slot at `+0x70/+0x74`, the first of the six die pictures), so every counted die starts as `image/die1` whatever body art it was built with. The Lua creates `die1..die4`, so **N = 4**; the final-face array can hold only four values before it would overlap the picture slots at `+0x70`. All final values are zeroed. Then four children are **disabled (hidden)**: `win_message`, `lose_message`, `draw_message`, `stop_label`.

### 1.4 The Lua that builds and uses it (`sim/character.lua`, `LCharacter:PlayDice`, function `main.16`, Lua lines 237-340)
The character table entries `gambler` and `trickster` (`data/travelers.lua`) have `action = PlayDice`. `PlayDice(self)`:

1. **Eligibility.** If `gSim.rank < 1` or `gSim.money < 500` it returns `false` (the game is not offered).
2. **Wager.** Let `M = gSim.money`.
   `w = Floor(bsutil.random(M/10, M*3/4) / 100) * 100` (`bsutil.random` is the CRT-`rand()` based inclusive helper of `choc1-natives.md`, so it also consumes `rand()` draws before the dice do);
   `cap = Floor(M * 0.2)`;
   if `w < 100` then `w = 100`; **elseif** `w > 250000` then `w = 250000`; **elseif** `cap < w` then `w = cap`;
   finally `w = Floor(w / 100) * 100`. Note the `elseif` chain: the 20% cap is applied only when the wager is between 100 and 250000; a wager above 250000 is set to 250000 without comparing to the cap. (Chocolatier 1 gives the player **no choice of stake**: the opponent names it.)
3. **Window.** A `Window` named `dice` (x=101, y=10, w=597, h=301+45) with a `Rectangle` and a `DiceGame{x=0,y=0,w=kMax,h=kMax}` containing: `Bitmap`s `die4` (x=392,y=165), `die3` (447,165) on the top row and `die2` (392,223), `die1` (447,223) on the bottom row (positions computed from the bytecode constants; relative to the `DiceGame` child at (0,0)); each `image/die1`, 50x50; a `Text` labelled `#<dice_myroll with character name>` on the top row (right-aligned, 50 high) and `#<dice_yourroll>` on the bottom row; `Text{name="intro"}` with `GetVariableString("dice_intro", {wager=Dollars(w)})`; `Text` `win_message`, `lose_message`, `draw_message` (keys `dice_win`, `dice_lose`, `dice_tie`); a `Button "stop"` (command `StopDie()`) and `Text "stop_label"` (key `dice_stop`) at the same place as `Button "roll"` and `Text "roll_label"` (`dice_roll`); the roll button's command is the closure below; `Button "leave"` (`close=true`, `exitButtonGraphics`) with `leave_label`; and the four `image/dchar_*` frame pieces. It is shown with `DisplayDialog{"ui/dialog.lua", sound="charopen", body=...}` and `PlayDice` returns `true`.
4. **Roll button closure** (`main.16.1`): `UtilDisableWindow` on `roll`, `roll_label`, `leave`, `leave_label`; `UtilEnableWindow` on `stop`, `stop_label`; `RollDice(tostring(wager))`; `UpdateStandardUI()`.

Which dice are whose (fact from the Lua and the native together): `die1` and `die2` (bottom row, label "your roll") are the **player's**; `die3` and `die4` (top row, "my roll", spoken by the character) are the **character's**. The native's win test uses final values 0 and 1 against 2 and 3 (see 1.5).

There is only one roll per dialog: `roll` is disabled by the closure and never re-enabled; `leave` returns when the contest ends.

### 1.5 `RollDice(betText)` and the animation (facts)
`RollDice` (`00408a30`):

1. For `i = 0 .. N-1` in order: `final[i] = rand() % 6` (CRT `rand()`, 15 bits; a faint bias towards 0 and 1 because 32768 is not a multiple of 6).
2. Store the wager text, set `elapsed = 0`.
3. Play the roll sound (`dice_game_roll.ogg`; the play call's third argument is 0, its meaning was not decoded).
4. Start a **repeating 62 ms task** (`0x3e`) calling the tick.

The tick (`00408b40`), once per 62 ms, in order:

1. `elapsed += 62`.
2. `remaining = N`; if `elapsed > 2000` then `remaining = N - (elapsed - 2000) / 750` (unsigned integer division).
3. **Redraw the faces** (`00408910(remaining)`): for each die `i` in order `0..N-1` it finds the child `die<i+1>`, **draws `rand() % 6`** (always, one draw per die per tick, in order), and shows image index `rand` if `i < remaining`, otherwise the fixed `final[i]`. So the **highest-numbered dice stop first**: `die4`, then `die3`, `die2`, `die1`.
4. If `remaining != 0` the tick returns true (keeps running).
5. If `remaining == 0` (done): stop the roll sound; disable `stop` and `stop_label` (note: the C++ disables the *named windows* `stop`, `stop_label`); compute the two sums `A = final[0] + final[1]` (player) and `B = final[2] + final[3]` (character).
   * **`B < A` (player wins):** disable `intro`; enable `leave`, `leave_label`, `win_message`; play the win sound; later run `gSim:AdjustMoney(<wager>,'dice_logwin')`.
   * **`A < B` (player loses):** the same with `lose_message`, the lose sound, and `gSim:AdjustMoney(-<wager>,'dice_logloss')`.
   * **Equal:** disable `intro` (the same as the other two branches); enable `leave`, `leave_label`, `draw_message`, play the tie sound, no money statement.
   The statement is built as a string (`"gSim:AdjustMoney("` + wager text + `",'dice_logwin')"`, or with a leading `"-"` before the wager text for losses) and executed with `DoString` (`0043b220`) **after** the sound call. Because the wager text is pasted unchanged, it must be a plain number string as produced by `tostring(w)` (the Lua passes whole numbers, so `"500"`, never `"5e+05"`: `%.14g` prints whole values below 10^14 without an exponent).
   The tick returns `false` when `remaining == 0`, which stops the task.

Resulting timeline with all 62 ms ticks (N = 4):

| Event | Condition | First tick time |
|---|---|---|
| all dice random | `elapsed <= 2000` | ticks 1..32 (62..1984 ms) and tick 33 at 2046 ms (`2046 - 2000 = 46 < 750`, so `remaining` is still 4) |
| `die4` stops | `elapsed >= 2750` | tick 45 (2790 ms) |
| `die3` stops | `elapsed >= 3500` | tick 57 (3534 ms) |
| `die2` stops | `elapsed >= 4250` | tick 69 (4278 ms) |
| `die1` stops, contest ends | `elapsed >= 5000` | tick 81 (**5022 ms**) |

So one die every 750 ms (rounded up to the next 62 ms tick) after a 2 s spin, 5.02 s in all if never stopped.

**RNG order** (reproducible): 4 draws in `RollDice` (final faces); then 4 draws per tick (the draws for stopped dice are discarded); a full 81-tick run therefore uses 4 + 81*4 = 328 draws. Every `StopDie` press adds 4 draws immediately (see below) and shortens the run.

### 1.6 `StopDie()` (`004089e0`)
Meant for the player's Stop button while rolling. It computes `remaining` exactly as the tick does (using the current `elapsed`; for `elapsed <= 2000` it is `N`). If `remaining > 0` it **redraws the faces once** (the same function, `remaining`; 4 draws) and then sets `elapsed = (N - (remaining - 1)) * 750 + 0x792` (0x792 = 1938). The next tick adds 62, reaching `2000 + (N - remaining + 1) * 750`, so `remaining` falls by exactly one and **the next die stops on the very next tick (62 ms later)**. Each press therefore stops one die: from `remaining = 4` it jumps `elapsed` to 2688 (the next tick is 2750); from 3 to 3438 (3500); from 2 to 4188 (4250); from 1 to 4938 (5000). If `remaining` is already 0 it does nothing. Pressing before `RollDice` runs (when `elapsed = 0`) only redraws and sets `elapsed = 2688`; no task runs, so nothing visible happens (the roll button covers the stop button until the roll starts).

### 1.7 Money and the Lua
`gSim:AdjustMoney(amount, reason)` is called from inside the native via `DoString`; there is no Lua callback after the dice stop, so the dialog's `leave` button simply closes. The money change appears when the contest ends. The player's stake is never taken up front, so **the balance is untouched until the end**. While the dice roll the `leave` button is disabled, so the dialog offers no way out until the contest ends.

### 1.8 Assets (fact)
`image/die1..die6.png` 50x50; frame art `image/dchar_left.png` 64x326, `dchar_right.png` 64x326, `dchar_top.png` 469x37, `dchar_bottom.png` 469x66; sounds `audio/sfx/dice_game_roll|win|lose|tie.ogg`.

### 1.9 Facts added while porting (dice)

* FACT (read from the decompile of the redraw `00408910`): the loop draws `rand() % 6` for every die **before** looking at `remaining`, and `if (remaining <= i) value = final[i]`; the shown index is then used **directly as the picture slot** (`this + value*8 + 0x70`), i.e. a face `v` in 0..5 shows the `v`-th of the six loaded `die1..die6` pictures - image/die<v+1>. The contract of 1.2 ("face value v shows image/die<v+1>") is confirmed at the instruction level.
* FACT (read from the disassembly of the tick `00408b40`): all three outcome branches disable `intro` (the tie branch too), then enable `leave` and `leave_label` and one of the three messages. The money statement is run only on the win and lose branches, after the outcome sound, and a tie pays nothing.
* FACT (found in `sim/character.lua`'s `LCharacter:DoAction` while making the encounter playable, main.12 at character.lua:141): `DoAction(self, building)` is what `LBuilding:UISelect` reaches, and it **diverts to speech before ever calling the character's `action`**: with `gSim.rank == 0` it runs `Converse("tutorofftrack")`; with quests active and `LQuest._Variable.QuestCounter == 0` it speaks `qi_novicenudge`; every 12 sim weeks (when `gSim.nextHintTime <= gSim.time`) it speaks the current quest's `qh_<name>` hint instead. Only past those gates does it run the character's `action` function (the gambler's `action = PlayDice`, a global function reference taken from `character.lua`, not a string). The travel building's `character` field is an array of character objects, and `ChooseCharacter` picks `self.character[bsutil.random(1, getn(self.character))]`, so a port can pin the meeting by setting that array. `UiMathRandom` (`bsutil.random`) is *not* the CRT stream in the port (it maps to RandRange there); the dice themselves stay on the shared CRT stream of 1.5.
* FACT (the port's own run): a traveller encounter's dice dialog stays on top of the map while the trip's queued `ArriveAtPort` waits for the dialog to close (the main script drains `gCommandQueue` one command per `WaitForMessage`); the fare is charged on arrival as always, i.e. only after the contest is settled and `leave` pressed.
* INFERENCE (engine gap, port note): the original keeps sound *handles* (+0x34..+0x4c) and stops the roll through its handle (`FUN_00433df0`). The engine's PlaySound path has no handle, so the engine adds `Audio::stopFile(path)`, which stops every voice playing `path` at once; matching by file is the closest equivalent and only that one sound file is stopped.

## 2. Chocolatier 2 `DiceGame` (Lua contract; native not decoded)

### 2.1 Eligibility and entry (`sim/character_actions.lua`, `PlayDice`)
`PlayDice(c, d)`: if `d` is set it returns the string `"PlayDice"`. Otherwise the game is offered only if `gSim.money > 5000` **and** (`gSim.rank >= 4` **or** `gSim.weeks - gSim.gamble > 1`); else it returns `false`. When offered: `DisplayDialog{"ui/chartalk.lua", body="gamble_dice_yn", char=c, yes="yes", no="no"}`; on yes `gSim.gamble = gSim.weeks` (so a low-rank player waits at least two weeks between gambles; rank 4 and up has no limit) and `DisplayDialog{"ui/gamble_dice.lua", char=c}`; on no (and `fpWrite` exists) `fpWrite{"DiceGame", weeks, money, 0,0,0}`. It returns `true`. `PlayBonus` (crates) has the same gate and uses `gamble_crate_yn` / `ui/gamble_bonus.lua`; `gSim.gamble` is shared by both games.

### 2.2 Bet limits (`ui/gamble_dice.lua` top, identical wording to the crates game except two lines)
* `minBet = Floor((Floor(M * 0.01) + 50) / 100) * 100`, clamped to at least 100 and at most 1,000,000. This is also the **ante**.
* `maxBet = Floor(M * 0.2)`; if `maxBet < minBet` then `maxBet = M * 0.5`; if `maxBet > 1,000,000` then `1,000,000`; if `maxBet < minBet` then `maxBet = minBet`; if `minBet == maxBet` then `maxBet = 2 * minBet`.
* `deltaBet = Floor((maxBet - minBet) / 10)` rounded to the nearest 100 (`Floor((x + 50) / 100) * 100`); `maxBet = minBet + 10 * deltaBet`.
* If `maxBet > M / 2`: `maxBet = Floor((M / 2) / 100) * 100`, `deltaBet = Floor((maxBet - minBet) / 10)` (not re-rounded), `maxBet = minBet + 10 * deltaBet`.
* If `maxBet < minBet`: `maxBet = minBet`, `deltaBet = 0`.
* `ante = minBet`; `bet = minBet`. (Chocolatier 3's crates game starts at `minBet + 5 * deltaBet`; the Chocolatier 2 dice game starts at the minimum.)

(The dice code differs from `gamble_bonus.lua` in three places: it has no "`minBet == maxBet` and `maxBet > 200` halves `minBet`" clause (the equal case doubles `maxBet` instead), it has no `deltaBet <= 0` recovery clause (`minBet = maxBet - 1000`), and it starts at `minBet`.)

### 2.3 Dialog
A `Frame` (x = 100 + ui.xOffset, y = 50 + ui.yOffset, 600x400, fit) with the `instructions` text (`gamble_dice_instructions_1`, "We'll each roll 4 die, and the highest 4-dice total wins. It will cost you %1% to play, but you will have a chance to add to your bet after you have rolled your first 2 dice."), `TextButton`s `cancel` and `done` (both `close=true`), `Text ante` (y=255), `Text pot` (y=275, 32 pt, shows `Dollars(bet)`), two arrow `Button`s `bet_more` (scroll-up art) and `bet_less` (scroll-down art) at y=275 and y=300, and a `DiceGame{x=kCenter, y=kCenter, w=kMax-charW, h=kMax, fit}` with: `Text "character"` (y=170, the opponent's `name_roll` label), eight `Bitmap`s `die_4..die_7` (the opponent's, y=190, x = 103 + 50*k) and `die_0..die_3` (the player's, y=290), all initially `image/die_unknown` (50x50), `Text "player"` (y=270), and `TextButton`s `stop` (label `roll_stop`, command `StopOne()`), `start` (label `roll_dice`, command = first-roll closure, sound `sfx/roll_dice.ogg`) and `roll` (label `roll_dice`, second-roll closure, the same sound).

### 2.4 Flow
1. `UpdateBet()`: label `pot`; `bet_less` enabled iff `minBet < bet`; `bet_more` iff `bet < maxBet`. The arrows step `bet` by `deltaBet`. At the end of setup the Lua disables `roll_dice` (a name no window has; effectively a no-op), `bet_more`, `bet_less`, `done`, `roll` and `stop`, leaving `start` and `cancel` live.
2. **First roll** (`start`): `gDiceRollCount = 0`; disable `start` and `cancel`; `SetLabel("ante", GetString("your_bet"))`; **`gSim:AdjustMoney(-ante)`** (the ante is paid now); enable `stop`; `RollFirst()`; loop `Pause(50)` until `gDiceRollCount >= 2`; disable `stop`; enable `roll`; set the `player` label to `name_roll(gSim.player) .. ": " .. tostring(PlayerTotal())` and `character` to `name_roll(char.name) .. ": ??"`; `instructions = gamble_dice_instructions_2(PlayerTotal(), Dollars(ante))` ("Your first two dice total %1%. ... Raise your bet if you'd like, then click roll again"); `UpdateBet()` (now `bet_more` and `bet_less` are usable).
3. **Second roll** (`roll`): disable `bet_more`, `bet_less`, `cancel`, `roll`; enable `stop`; `RollSecond()`; loop `Pause(50)` until `gDiceRollCount >= 8`; disable `stop`; `playerTotal = PlayerTotal()`, `charTotal = CharacterTotal()`; labels updated (`name_roll`: total for both sides); `stake = bet - ante`.
   * `playerTotal < charTotal` (loss): `gSim:AdjustMoney(-stake)` (the rest of the bet beyond the ante); `winnings = -bet`; `PlaySound("dice_lose")`; instructions `char_wins(ante, bet)` ("Looks like I win %2%").
   * `charTotal < playerTotal` (win): `gSim:AdjustMoney(ante + bet)` (the ante back plus the bet); `winnings = bet`; `PlaySound("dice_win")`; `player_wins(ante, bet)`.
   * **Tie:** no money movement; `winnings = -ante` (the ante is lost); instructions `tie(ante)` ("I'll keep the %1%, and you keep the rest").
   * Net result: win `+bet`, loss `-bet`, tie `-ante` (the opponent wins ties).
   * Then `gSim:Message(...)`: `winnings > 0` -> `message_wondice(Dollars(winnings))`; `winnings < 0` -> `message_lostdice(Dollars(-winnings))`; `fpWrite{"DiceGame", weeks, money, ante, bet}` if `fpWrite` exists; enable `done` (which closes the dialog).
4. `cancel` closes without playing (only before the first roll).

### 2.5 Natives, as the Lua uses them (Unknown beyond this)
`RollFirst()`, `RollSecond()`, `StopOne()`, `PlayerTotal()` (number), `CharacterTotal()` (number), the global `gDiceRollCount` that the native counts up (one per die that has stopped, inference), and the window type `DiceGame` with children named `die_0..die_7`.

**Inference from the counts and labels** (not decoded): `RollFirst` rolls and stops the player's first two dice (`die_0`, `die_1`; counter reaches 2); `PlayerTotal()` then returns the total of the player's revealed dice; the opponent's dice stay `die_unknown`; `RollSecond` rolls the rest (the player's other two and all four of the opponent's; counter reaches 8); the totals are the sums of `die_0..die_3` and `die_4..die_7`. `StopOne` stops one die per press. The animation timing, the number of random draws, the RNG and the die image names (`image/die1..die6`, `die_unknown`, all 50x50 PNG) are not known from the Lua. The Chocolatier 1 timings (62 ms tick, 2 s, 750 ms) may have been kept but nothing proves it.

Assets (Chocolatier 2): `image/die1..die6.png`, `image/die_unknown.png` (all 50x50), `sfx/roll_dice.ogg`, `sfx/dice_win.ogg`, `sfx/dice_lose.ogg` (no tie sound; the Lua plays none).

## 3. `BonusGame` against `CratesGame`

Chocolatier 2 ships `glitz/sparkle.lua`, `sparkle.png`, `sparkle.xml` and `money.png`; the engine keeps the crate sparkle stubbed (a follow-up), as for Chocolatier 3.

The Chocolatier 3 game is documented in `game-windows-questions.md` section 6 and `ledger-and-game-natives.md` 10.1 (`TBonusGame`, Lua type `CratesGame`, 21 crates). The Chocolatier 2 Lua `ui/gamble_bonus.lua` creates `BonusGame{x=0,y=0,w=kMax,h=kMax, Group(selections)}` with 25 buttons. The two Luas are the same program with these differences:

| | Chocolatier 2 `BonusGame` (`gamble_bonus.lua`) | Chocolatier 3 `CratesGame` (`ui_crates.lua`) |
|---|---|---|
| window type | `BonusGame` | `CratesGame` (the C++ class is `TBonusGame`) |
| crates | 25: `select_1..select_25` | 21: `select_1..select_21` |
| grid | 5 columns, `x = ((i-1) mod 5) * 60 + 60`, `y = Floor((i-1) / 5) * 60 + 120`, buttons 50x47 | 7 columns, `x = ((i-1) mod 7) * 60 + 226`, `y = Floor((i-1) / 7) * 55 + 95`, 50x47 |
| `bet` limits | `minBet`, `maxBet`, `deltaBet` code the same (including the `minBet == maxBet and maxBet > 200` halving, the 50% clamp and the "screwy numbers" clamp), initial `bet = minBet + 5 * deltaBet` | the same |
| money out | `gSim:AdjustMoney(-bet)` at `AcceptBet` | `Player:SubtractMoney(bet)` |
| money in | `Done`: `gSim:AdjustMoney(winnings)` (not on a tie, not on a loss) and `Message(...)` | `Player:AddMoney(winnings)` (the same cases) |
| messages | `message_tiecrates`, `message_lostcrates`, `message_woncrates` (via `gSim:Message`) | none |
| telemetry | `fpWrite{"CratesGame", weeks, money, bet, winnings}` on Cancel/Done | none |
| first-crate hint | the same rounding to 10000 / 1000 / 100 of `PrepareGame(bet)`'s total, string `gamble_crate_first` | the same |
| natives | `PrepareGame(bet)` -> total, `SelectPrize(i, final)` -> value, `SetCommand(name, fn)` | the same (`SelectPrize` returns 0 for a whammy) |
| string keys | `gamble_crate_instructions`, `crates_ahead`, `crates_behind`, `crates_even`, `crates_lost`, `crates_welldone`, `losings`, `winnings`, `you_won`, `you_lost` | the same |
| dialog | a `Frame` 600x400 at (100,50)+offsets with the `Visual` portrait | a 701x366 `Window` over `image/popup_back_market`, with `C3...Style` |
| tie quirk | the tie branch does **not** credit the winnings back, so the player loses the bet while the message says "broke even" | the same (the tie branch only sets labels) |

The shared quirk is a fact in both: in `Done`, `won = winnings - bet`; if `won == 0` nothing is credited (the bet was already deducted at `AcceptBet`); if `winnings == 0` it is a loss message; otherwise `winnings` is credited, even when it is less than the bet (a net loss still prints the "won" message in Chocolatier 2).

What the Chocolatier 2 art suggests (inference): the crate sprites are `image/icon_crate.png`, `icon_crate_open.png`, `icon_crate_front.png` (50x47, the button size), a larger set `large_crate*.png` (100x94) and `image/whammy_devil.png` (128x128). Chocolatier 3 uses `box_closed`, `box_open`, `box_open_mask` (133x135) and `whammy_devil`, so the drawing differs even though the Lua does not. The 25-crate prize factors, the number of whammies and the animation timings of `BonusGame` are not visible (Unknown); a port could start from the Chocolatier 3 table (`ledger-and-game-natives.md` 10.1: 21 factors summing to 5.125 with three zeros, a single-pass `rand() % 21` shuffle) scaled to 25, but this is a guess and must not be presented as fact.

`docs/design/choc2-minigames.md` 2.6 treats `BonusGame` as nearly a rename of `CratesGame`; the Lua confirms that for logic and string keys, and shows the crate count, the grid and the art differ.

## 4. Port notes (short)

* Chocolatier 1 dice: model it as `N`, `final[]`, `elapsed`, the 62 ms tick and the order of `rand()` draws above if you want the same sequences; if you only need behaviour, note the thresholds 2000 / 750 and that `StopDie` makes the next die stop on the next tick.
* The money statement is executed after the result sound; a port can call `AdjustMoney(+/-wager, 'dice_logwin'|'dice_logloss')` directly and skip the string building.
* Chocolatier 2 dice can be written entirely from section 2 once `RollFirst`/`RollSecond`/`StopOne` are defined; choose the timings from section 1 as an inference and say so.

## 5. Unknown

* Whether `UtilDisableWindow("stop")` etc. act on exactly the first window of that name in the whole tree when several share the name (the roll and stop buttons are different names, so it is unaffected here).
* Why `StopDie` and `RollDice` exist as globals only while a `DiceGame` window lives: the destructor clears them, but nothing in Lua depends on that.
* Whether the dice `roll` sound loops (the native stops it explicitly at the end; the play call's third argument is 0 and was not decoded).
* Everything about the Chocolatier 2 dice and crates natives (timings, RNG, the crate prize factors and number of whammies, `gDiceRollCount` semantics, what `StopOne` does when nothing is rolling).
* The initial visible state of the Chocolatier 1 `stop` button (not hidden by `Init`, but covered by the later-created `roll` button).
* Where `UtilMathSeedRandom` is called for a game (the dice do not seed `rand()` themselves).
