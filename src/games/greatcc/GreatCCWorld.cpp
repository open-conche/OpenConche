#include "games/greatcc/GreatCCWorld.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gfx/text/Font.h"
#include "gfx/text/TextLayout.h"
#include "script/AudioBridge.h"

namespace choco::games::greatcc {

namespace {
// The font compiled into the original's executable (an MVC4 blob of 0x12c2c bytes; fonts RE 1.8), read from the user's own
// copy beside the asset folder: <assets>/../*.exe on Windows, <assets>/../../MacOS/* in the Mac bundle.
std::shared_ptr<const gfx::text::Font> findEmbeddedFont(const std::string& assetsRoot) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root = fs::path(assetsRoot);
  std::vector<fs::path> files;
  for (const fs::path& dir : {root / "..", root / ".." / ".." / "MacOS"}) {
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      const bool exe = it->path().extension() == ".exe" || it->path().extension() == ".EXE";
      if (exe || dir.filename() == "MacOS") files.push_back(it->path());
    }
  }
  constexpr std::size_t kBlob = 0x12c2c, kMaxFile = 64u << 20;
  for (const fs::path& p : files) {
    const auto size = fs::file_size(p, ec);
    if (ec || size > kMaxFile) continue;
    std::ifstream in(p, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (std::size_t at = data.find("MVC4"); at != std::string::npos; at = data.find("MVC4", at + 1)) {
      const std::size_t n = std::min(kBlob, data.size() - at);
      std::vector<std::byte> bytes(n);
      std::memcpy(bytes.data(), data.data() + at, n);
      if (auto font = gfx::text::Font::fromBytes(std::move(bytes), "__EMBEDDED")) return font;
    }
  }
  return nullptr;
}
}  // namespace

GreatCCWorld::GreatCCWorld(script::ScriptContext& ctx)
    : ctx_(ctx),
      values_(progress_),
      upgrades_(progress_),
      quests_(progress_, values_, upgrades_,
              [&ctx](const std::string& key, const std::vector<std::string>& args) { return ctx.strings.sdkLookup(key, args); }),
      plots_(ctx),
      levels_(progress_, values_),
      achievements_(progress_, values_),
      notices_(ctx, achievements_),
      tutorials_(ctx, progress_, settings_, gameStates_),
      transitions_(ctx),
      sound_(ctx.audio) {
  // The window manager's hooks reach the transition manager through the context, never through a stored world pointer
  // (design 3.2). They are never removed: the App destroys the window manager before the game state, so the destructor must
  // not touch it, and a hook that outlives this world finds no world and does nothing.
  ctx.wm.setInputFilter([&ctx](const ui::InputEvent& ev) {
    const GreatCCWorld* w = worldOf(ctx);
    return w && w->transitions_.handleEvent(ev);
  });
  ctx.wm.setMessageHook(kFirstTransitionMessage, kLastTransitionMessage, [&ctx](const ui::Message& m) {
    GreatCCWorld* w = worldOf(ctx);
    return w && w->transitions_.onMessage(m);
  });
  ctx.wm.setOverlayDrawer([&ctx](ui::DrawContext& dc) {
    if (GreatCCWorld* w = worldOf(ctx)) w->transitions_.draw(dc);
  });
  // The value modifiers that name an upgrade ask the upgrade manager (VValue::GetValue).
  values_.setUpgradeActive([this](const std::string& name, const std::string& category) { return upgrades_.isActive(name, category); });
  values_.setPlotActive([this](const std::string& name) { return plots_.isPlotItemActive(name); });
  if (gfx::text::TextSystem* text = ctx.wm.textSystem()) {
    auto font = findEmbeddedFont(ctx.fs.assetsRoot());
    ctx.log.write(font ? "[greatcc] embedded font from the game's executable" : "[greatcc] no executable beside the assets: Myriad stands in for its embedded font");
    if (!font && ctx.fs.exists("fonts/myriad_pro.mvec")) font = text->fonts.get("fonts/myriad_pro.mvec");
    text->fonts.setEmbedded(std::move(font));
  }
  settings_.fixBugs = ctx.config.get("fixbugs") != "0";
  ctx.log.write(std::string("[greatcc] fixbugs ") + (settings_.fixBugs ? "on" : "off"));
}

// The progress holds its own reference to the user store, so this last write is safe at shutdown; it touches no window.
GreatCCWorld::~GreatCCWorld() { progress_.flush(); }

void GreatCCWorld::tick(double nowMs) {
  lastNowMs_ = nowMs;
  transitions_.tick(nowMs);
  progress_.flush();  // one write per frame at most, when something changed
}

void GreatCCWorld::setForeground(bool foreground, double) {
  foreground_ = foreground;
  if (!foreground) pauseInsideGame();
  if (ctx_.audio) ctx_.audio->setForeground(foreground);
}

void GreatCCWorld::pauseInsideGame() {
  if (!settings_.insideGame) return;
  settings_.insideGame = false;
  doLuaString(ctx_, "DoModal( 'scripts/pause.lua' );");
}

void GreatCCWorld::init(script::ScriptContext& ctx) {
  progress_.attach(ctx.users);
  auto readAsset = [&ctx](const char* path) {
    auto text = ctx.fs.readText(path);
    if (!text) ctx.log.write(std::string("[greatcc] ") + path + " is missing");
    return text.value_or(std::string());
  };
  if (!values_.load(readAsset("values.xml"))) ctx.log.write("[greatcc] values.xml did not load");
  if (!levels_.loadGoals(readAsset("levels/goals.xml"))) ctx.log.write("[greatcc] levels/goals.xml did not load");
  if (!achievements_.load(readAsset("achievements.xml"))) ctx.log.write("[greatcc] achievements.xml did not load");
  if (ctx.users) {
    const int mode = std::atoi(ctx.users->globalPref("gamemode", "0").c_str());
    settings_.setMode(mode);
  }
  reloadState(ctx);
}

void GreatCCWorld::reloadState(script::ScriptContext& ctx) {
  if (!progress_.load())
    ctx.log.write("[greatcc] the progress file is not readable; kept as greatcc-progress.txt.bad, starting from defaults");
  values_.reload();
  upgrades_.clear();  // the next level load reads the new player's purchases
  achievements_.reload();
  tutorials_.loadCurrentUser();
  quests_.loadCurrentUser();
}

void GreatCCWorld::createUser(script::ScriptContext& ctx, const std::string& name) {
  if (!ctx.users) return;
  progress_.flush();
  if (ctx.users->create(name) < 0) {
    ctx.log.write("[user] CreateNewUser: rejected the name (empty, too long, bad UTF-8 or control characters)");
    return;
  }
  script::applyAudioPrefs(ctx);  // a new user starts from the previous user's sound prefs
  reloadState(ctx);
}

void GreatCCWorld::setCurrentUser(script::ScriptContext& ctx, int index) {
  if (!ctx.users) return;
  progress_.flush();
  if (!ctx.users->setCurrent(index)) return;  // ignored for an index >= the user count
  script::applyAudioPrefs(ctx);
  reloadState(ctx);
}

void GreatCCWorld::deleteUser(script::ScriptContext& ctx, int index) {
  if (!ctx.users) return;
  progress_.flush();
  if (!ctx.users->remove(index)) ctx.users->setCurrent(index - 1);  // out of range: still SetCurrentUser(index - 1)
  script::applyAudioPrefs(ctx);
  reloadState(ctx);
}

void GreatCCWorld::logScore(script::ScriptContext& ctx, long long score, const std::string& a, const std::string& b) {
  if (!ctx.users) return;
  ctx.users->logScore(static_cast<double>(score), a, b, settings_.hiscoreMode());
}

void GreatCCWorld::renameCurrentUser(script::ScriptContext& ctx, const std::string& name) {
  if (!ctx.users) return;
  const int index = ctx.users->current() >= 0 ? ctx.users->current() : 0;
  struct Best {
    const char* mode;
    long long score;
    std::string a;
  };
  std::vector<Best> best;
  const std::string old = ctx.users->count() > 0 && index < ctx.users->count() ? ctx.users->name(index) : std::string();
  for (const char* mode : kModeNames)
    for (const sdk::HiscoreRow& row : ctx.users->hiscores().rows(mode))
      if (row.user == old) {
        best.push_back({mode, row.score, row.a});
        break;
      }
  if (!ctx.users->rename(index, name)) {
    ctx.log.write("[user] ChangeCurrentUserName: nothing renamed");
    return;
  }
  const std::string medals = achievements_.medalXml();
  for (const Best& b : best) ctx.users->logScore(static_cast<double>(b.score), b.a, medals, b.mode);
}

void GreatCCWorld::setGameMode(script::ScriptContext& ctx, int mode) {
  if (settings_.setMode(mode)) persistGameMode(ctx);
}

void GreatCCWorld::persistGameMode(script::ScriptContext& ctx) {
  if (ctx.users) ctx.users->setGlobalPref("gamemode", std::to_string(settings_.mode));
}

bool cheatModeOn(script::ScriptContext& ctx) { return std::strtol(ctx.config.get("cheatmode").c_str(), nullptr, 10) != 0; }

GreatCCWorld* worldOf(script::ScriptContext& ctx) { return dynamic_cast<GreatCCWorld*>(ctx.gameState.get()); }

}  // namespace choco::games::greatcc
