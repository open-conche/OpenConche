# High Scores screen (vanilla v1.0): `ui/hiscore.lua` + native `HiscoreWindow` (`THiscore`)

Sources: Mac decompile `re/mac/decomp/THiscore.c`, `TPfHiscores.c` (addresses below are Mac VM addresses, as in
`re/mac/index.tsv`); vanilla Lua disassembled from `re/choc3/pfp_extracted/ui/hiscore.lua` (bytecode, run through
a workbench script; line numbers are the original Lua source lines recorded in the chunk). Related: `ledger-and-game-natives.md`
section 8 (score store, ParseGameData, Submit* natives) and `api-globals.md`; this file adds the screen-level behaviour.
No native table worth a .tsv: the only tables are the Lua constants below and the categories, which come from the server.

## 1. Which Lua builds the screen, and what it calls

- Opened by `DisplayDialog { "ui/hiscore.lua" }` from the main menu `HighScores` function (`ui/mainmenu.lua` button `high_scores`) and from
  the pause menu (`ui/ui_pause.lua`, button callback at source line 29). Reforged also opens it from `ui/community_hub.lua:114`.
- `ui/hiscore.lua` main body, in order: `Player:LogScore()` (line 2); fonts (globals `HeaderFont`, `TableInfoFont`, `EligibleFont`,
  `EligibleAsteriskFont`, `ConnectingToServerFont`, `TableHeaderFont`, `ScoreInfoFont`, `ScoreFont`, `PlayerInfoFont`, `RankFont`, all
  `standardFont` at sizes 70/30/18/12/18/30/18/18/14/26; colours Blue for `HeaderFont`, Black for the rest); `LeftButtonGraphics`
  (`hiscore/arrowleft_up|down|over`); layout globals `kScoreRowSpace=32, kRowY=95, kLeftX=5, kNumberX=5, kNameX=57, kInfoX=57, kScoreX=470,
  kP1X=5, kNameW=400, kNameH=21`; state enum globals `eLocalView=0, eRequestingCategories=1, eRequestingScores=2, eSubmitting=3,
  eGlobalView=4, eError=5`; global function `UpdateButtons` (line 40); then `MakeDialog` with the window `hiscorescreen` (x=1000, y=9, fit)
  and finally `CenterFadeIn("hiscorescreen")`.
- `Player:LogScore()` (sim/player.lua): see ledger doc 8.1. In vanilla this is the **only** place a score is logged: no game-over or
  Free-Play-end hook calls `LogScore` (the constants of vanilla `sim/player.lua` mention it only inside the method itself). So the score of the
  current game enters the table when the player opens High Scores (main menu with a loaded player, or the pause menu). Score is
  `Floor(money / time)`, skipped when 0 (so with no game loaded nothing is recorded).
- Window tree (all coordinates as in the file): `Bitmap image/popup_back_highscores`; `HiscoreWindow` (w/h kMax) containing Texts `local`
  (label `localhighscores` = "High Scores") and `global` (label `globalhighscores`), both `TableHeaderFont`, centred, x=17 y=25 w=455 h=40;
  10 rows (see 2.4); `rightpanelsmall` (x=477): Texts `yourrankglobalinfo` (label `globalhighscoreinfo`), `yourrank`, `congratulations`
  (`congratshighscore`), `dnq` (`scorednq`); `rightpanel` (x=477, w=289): Texts `globalinfoheader` (`globalhighscores`), `info`
  (`globalhighscoreinfo`), `eligible` (`eligible`), Buttons `moreinfo` and `submit`, Texts `server` (label `connectingtoserver` =
  "Connecting to server", `ConnectingToServerFont`) and `error` (no label); Text `category`; Buttons `categoryleft`, `categoryright`
  (arrow graphics; right one `hflip`/rotate), `scrollup`, `scrolldown`; `LongButtonStyle` buttons `viewlocal` (label `viewlocal`, scale 1.1)
  and `view` (label `viewglobal`), both at x=731 aligned right. Also the nameplate Bitmap with Text `highscoreheader` ("HIGH SCORES") and
  an `ok` Button (default+cancel, closes with `FadeCloseWindow("hiscorescreen","ok")`).
- `moreinfo` command (line 193): `DoModal("ui/hiscoreinfo.lua")` (text `highscorecompleteinfo`, a link `privacypolicy` that calls
  `LaunchPrivacyPolicy`).
- `submit` command (lines 196-207): `vars = loadstring(GetLuaServerSubmitSetupVars(false)); vars(); val = DoModal("ui/serversubmit.lua")`;
  `val == "qualified"` -> `SubmissionDone(true)`; `val == "success"` -> `SubmissionDone(false)`.
- `UpdateButtons()` (source 40-76), called by the native after every update: `state = GetState()`; `localOnly = IsEnabled(kHiscoreLocalOnly)`;
  `anon = IsEnabled(kHiscoreAnonymous)`; `flag = (not localOnly) and (not anon)` (my reading of bytecode 8-13; the compiled TEST/JMP pattern is
  slightly ambiguous, so treat as likely). Disables `view, viewlocal, submit, categoryleft, categoryright`. State 0: `moreinfo` enabled iff
  `flag`; if not localOnly: enable `view`, and `submit` iff `ScoreAvailable()`. State 4: enable `viewlocal`, `categoryleft`, `categoryright`
  (`moreinfo` untouched). State 5: enable `viewlocal`. States 1-3: all stay disabled.
- Natives used from Lua: `GetState`, `ScoreAvailable`, `SubmissionDone`, `GetLuaServerSubmitSetupVars`, `LogScore` (via Player),
  `LaunchPrivacyPolicy` (hiscoreinfo), `EnableWindow`, `IsEnabled`, `DoModal`. `UpdateText` is not called from Lua. The Lua defines no
  handlers for view/viewlocal/scroll/category buttons: the native handles them by name (2.2).

## 2. Native behaviour (`THiscore`, `HiscoreWindow`)

### 2.1 Creation (ctor 0x2339e, `PostChildrenInit` 0x2566c)
Ctor: state=0, submitState=0, category index 0, scroll offset 0, error text empty; takes the settings' `TPfHiscores`. Registers the globals
`GetState`, `ScoreAvailable`, `UpdateText`, `LaunchPrivacyPolicy`, `SubmissionDone` (cleared again by the dtor except `LaunchPrivacyPolicy`).
`PostChildrenInit`: starts a 20 ms window animation, binds children `name1..10`, `score1..10`, `info1..10`, `1..10` (rank Text) and
`p1_1..10` (Bitmap) by exact name (missing ones stay null; `GetChildWindow` searches recursively), then `UpdateText(); UpdateButtons();`.
Window flag 0x100 = visible; all show/hide below is that bit.

### 2.2 Buttons (`OnMessage` 0x25432; handles message type 3 only, matched by the sender name, returns handled)
- `categoryleft` / `categoryright`: index -1 (wraps to count-1) / +1 (wraps to 0 when >= count); state := 2; `RequestScores(index)`;
  `UpdateText(); UpdateButtons()`.
- `view`: submitState := 0, state := 1, `RequestCategoryInformation()`, update. `viewlocal`: state := 0, update.
- `scrollup` / `scrolldown`: offset -10 / +10, then `UpdateText()` (clamping in 2.4). Unmatched messages return not-handled.
- `OnTaskAnimate` (every 20 ms, always returns 1): does nothing in states 0, 4, 5. In states 1 and 2 it polls
  `TPfHiscores::GetServerRequestStatus`: 2 = failure (stores the message, state := 5), 0 = done (state 1 -> 2 and `RequestScores(index)`;
  state 2 -> 4), 1 = pending. On a change: `UpdateText(); UpdateButtons();` and invalidate the screen.
- `SubmissionDone(qualified)`: state := 1, request categories, submitState := qualified ? 1 : 2, update.

### 2.3 Categories
Not a native list. `GetCategoryCount/Name` read what the server returned in `ParseCategoryData` (stored at `TPfHiscores+0x20/0x24`, 8-byte
entries, name string at +4). Names, count and order are server data: **unknown / not available offline** (count 0). The score request
asks for 500 rows (`limit=500`) of the selected category. The Text `category` shows the current category name in state 4 only.
Native config files for the server: `hiscore.xml` (`<hiscore>` children giving the server URL and game handle) and `hiscoreserver.txt`
(`http://www.playfirst.com/go/hiscore`, shipped next to the exe).

### 2.4 Rows (`UpdateScoresTable` 0x23e2e, `UpdateText` 0x247ea, `HideAllText` 0x238ae)
`UpdateText` always starts with `HideAllText`: hides all 50 row widgets and the named widgets `eligible, eligibleasterisk, server, error, info,
category, local, global, topplayers, congratulations, dnq, yourrank, yourrankglobalinfo, rankvalue, scrollup, scrolldown` (those that exist).
Local view (state != 4), up to 10 rows, offset forced to 0 (**no scrolling locally**, scroll buttons hidden), from `GetScore(local=true, i)`.
Global view (state 4): offset clamped to `0..count-5` (sic: count-5, not count-10), up shown if offset > 0, down shown if offset+10 < count.
Per row i (0..9), stopping at `min(count - offset, 10)`; later rows stay hidden:
- `nameN` Text (shown): stored user name. Then the font line height is reduced from 20 by 1 until the text bounds fit inside the Text
  rectangle (stops at 2): shrink-to-fit.
- `scoreN` (shown): `GetString("score_format", Dollars(score))` = "%1%/week", `Dollars` = money formatting with sign (e.g. "$1,234/week").
- `N` rank Text (shown): `"%d."` of the stored rank (tied scores print the same rank; ranks are 1, 2, 2, 4 ...).
- `infoN` (shown): `ParseGameData(a)` = `"%1%: %2% in %3% weeks"` with `rank_<d>` name, money, weeks (ledger doc 8.2).
- `p1_N` Bitmap (`hiscore/p1icon`, the PlayFirst-member badge): shown only when the row is **not** local and neither `hiscorelocal` nor
  `hiscoreanon` is set. Local-view rows are all flagged local, so the icon is never shown locally; it marks server-side registered users
  in global view.
Header Texts: local view shows `local` iff not `kHiscoreLocalOnly`, and `topplayers` (absent in the shipped Lua) iff local only. Global view
shows `global` iff not local only. Initial labels ("#####", "12345", "More info goes here") are replaced or hidden immediately.

### 2.5 Right panels (decoded from `UpdateText`)
- If `hiscorelocal` is on: both `rightpanel` and `rightpanelsmall` hidden; additionally `info`, `eligible`, `eligibleasterisk` hidden last.
- Else state 0: `rightpanel` shown, `rightpanelsmall` hidden. Any other state: `rightpanel` hidden, `rightpanelsmall` shown.
- State 0: `info` shown always; `eligible` and `eligibleasterisk` shown if the user has a stored eligible local score
  (`GetUserBestScore(0)`; `eligibleasterisk` is not in the shipped Lua, so nothing happens).
- State 4: `category` shown with the category name; `yourrankglobalinfo` shown, unless submitState 1 (shows `congratulations`, hides
  `yourrankglobalinfo`) or 2 (shows `dnq`, hides `yourrankglobalinfo`); if the user has a global best (`GetUserBestScore(1)`) `yourrank` is shown
  with `GetString("yourrank", rank)` = "Your Rank: %1%" and `yourrankglobalinfo` hidden.
- States 1-3: `server` ("Connecting to server") shown. State 5: `error` shown with the stored message text.
- **Observation to flag**: `server` and `error` are children of `rightpanel` (Lua nesting confirmed, same in Reforged), yet in states 1-3 and
  5 (non local-only) the same code hides `rightpanel`; by the rule "children of a hidden window are not drawn" (window-system.md) they would
  not be visible. I could not find a mechanism that avoids this. Treat it as unresolved (see Unknown); the original may have this quirk.

## 3. Global/online path and the safe offline behaviour
- `TPfHiscores` talks to `www.playfirst.com` over HTTP (`RequestCategoryInformation`, `RequestScores`, `SubmitData`); the server answered with XML.
  If the request fails the status call returns 2 and the window enters state 5 with the message; strings in the binary suggest the text is
  "Unable to connect to server. Please try again later." (string at 0x15b104 near the parse-error strings; **inference** that it is what
  the window shows; the exact status/message hand-off to `FillInErrorMessage` was not traced). `defaulterror` is also a server-side key.
- Vanilla `settings.xml` has no `hiscorelocal`/`hiscoreanon`, so both are off: `view` and `submit` are enabled in the local view. Reforged's
  `settings.xml` sets both to 0 as well.
- `submit` -> `GetLuaServerSubmitSetupVars(false)` + `DoModal("ui/serversubmit.lua")` -> `SubmitToServer` (ledger doc 8.3).
- Per the project rule (no bridge to PlayFirst servers), recommended stub, in order of fidelity:
  A (faithful): keep `hiscorelocal` off. `view` goes to state 1, then on the next animate tick to state 5 with the offline message; `viewlocal`
  returns to state 0. `submit` runs the dialog, whose `SubmitToServer` stub immediately calls `SwitchModes(false)` (no network).
  B (cleaner UI): make `IsEnabled(kHiscoreLocalOnly)` true by default for vanilla: then `view`/`submit` stay disabled, both right panels are
  hidden, and the screen is a pure local list. Side effect: `CheckConfig("local")` also turns true (Reforged `sim/player.lua` reads it).
  Both are project decisions; neither contacts a server.

## 4. Reforged
Reforged `ui/hiscore.lua` (890 lines) is a rewrite but still instantiates the native `HiscoreWindow` (line 789) with the same child names
(`name%d`, `score%d`, `info%d`, `%d`, `p1_%d`, `local`, `global`, `category`, `categoryleft/right`, `scrollup/down`, `rightpanel`,
`rightpanelsmall` incl. `yourrank/congratulations/dnq`, `server`, `error`, `info`, `eligible`, `submit`, `view`, `viewlocal`,
`moreinfo`), reads the native-filled rows back with `GetLabel("name"..i)` (line 555) and moves them (rows created at x=-2000), defines its own
`UpdateButtons` using `GetState`, `IsEnabled(kHiscoreLocalOnly)`, `ScoreAvailable` (line 689), and calls `GetLuaServerSubmitSetupVars(false)` (492),
`SubmissionDone(true|false)` (503/508), `Player:LogScore()` (line 11 and `community/local_scores.lua`, `serversubmit.lua:148`). Its
`Player:LogScore` native call is `LogScore(companyScore, "%d-%09d-%012.0f" data, medal xml)` where companyScore replaces cash/week, so the
native's "/week" suffix in `score_format` is a Reforged string-table concern. Also opens `ui/hiscore_details.lua` from the pause menu.
Therefore the native must stay behaviourally compatible with the vanilla child-name contract.

## 5. Local score store
Per ledger doc 8.1: `TPfHiscores::RecordScore` (0x45440) keeps up to 500 rows per game-mode table, sorted descending, one row per user
(higher replaces lower; equal or lower is ignored), saved by `SavePreferences` to `user:hiscore.dat`: an XML document encrypted with `TEncrypt`
(key from state `encryptionkey`) with a 4-byte checksum, loaded by `LoadPreferences` (message "Checksum did not match! Ignoring saved score
data" when it fails). Contains user info, local score tables (row attributes include rank, score, name, data string, medals, submitted flag)
and the medal table. The exact encryption/attribute names were not needed; the engine may use its own format (not shared with the
original). Rows hold: rank, score (int), user name, data string `R-WWWWWWWWW-MMMMMMMMMMMM`, medal XML string, game-mode name, local flag,
"does not qualify" flag, GUID.

## Unknown
- Which screen the user sees in states 1-3/5 given the `rightpanel` hiding quirk (2.5); whether `Connecting to server` was ever visible.
- The exact `moreinfo` enable condition in state 0 (see section 1; harmless dialog either way).
- Category names/count (server-defined); exact offline error string and its path through `FillInErrorMessage`.
- Encryption algorithm and attribute names of `hiscore.dat` (not needed).
- Where `eligibleasterisk` (string `eligibleasterisk`, "*Global score list maintained by PlayFirst...") was meant to appear: no such widget in vanilla Lua.

## What the engine must implement (for the implementer)
1. `HiscoreWindow` window class (child lookup as 2.1; ctor registers/dtor clears the five globals; `GetState` returns the state int).
2. Local view: rows from the local score store as in 2.4 (name, `score_format`+`Dollars`, `"%d."` rank, `ParseGameData` info, hide unused rows,
   no p1 icons locally, shrink-to-fit name), `local` header visibility, scroll buttons hidden.
3. `LogScore(score, a, b)` native with the store semantics in section 5 (in-memory + our own file under `--userdir`); `ScoreAvailable()`;
   `ParseGameData`; the string keys `score_format`, `score_info_format`, `rank_1..5` already exist in strings.xml.
4. `OnMessage` for the six named buttons and the 20 ms animation; right-panel visibility rules (2.5); `UpdateText` global; `SubmissionDone`.
5. Stubs: `TPfHiscores` network = permanent failure (status 2 + offline message), categories = 0, `GetLuaServerSubmitSetupVars` returns the safe
   string in ledger doc 8.3, `SubmitToServer` = logged no-op that runs `SwitchModes(false)`, `LaunchPrivacyPolicy` = no-op. Do not open URLs.
6. Decide A or B in section 3 and record it in the workbench decision log.
