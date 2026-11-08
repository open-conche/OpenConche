#include "app/ScriptDriver.h"

#include "app/Diagnostics.h"

#include <cstdio>
#include <sstream>
#include <utility>

namespace choco::app {

namespace {

bool readNumber(std::istringstream& in, double& v) { return static_cast<bool>(in >> v); }

bool noTrailing(std::istringstream& in) {
  std::string extra;
  return !(in >> extra);
}

}  // namespace

ParsedScript parseScript(const std::string& text) {
  ParsedScript out;
  std::istringstream lines(text);
  std::string raw;
  int lineNo = 0;
  while (std::getline(lines, raw)) {
    ++lineNo;
    if (auto hash = raw.find('#'); hash != std::string::npos) raw.erase(hash);
    std::istringstream in(raw);
    std::string cmd;
    if (!(in >> cmd)) continue;

    ScriptCommand c;
    c.line = lineNo;
    auto fail = [&](const std::string& msg) { out.errors.push_back("line " + std::to_string(lineNo) + ": " + msg); };
    auto coords = [&]() {
      double x, y;
      if (!readNumber(in, x) || !readNumber(in, y) || !noTrailing(in)) {
        fail("'" + cmd + "' needs: " + cmd + " x y");
        return false;
      }
      c.x = static_cast<float>(x);
      c.y = static_cast<float>(y);
      return true;
    };
    auto rest = [&](const char* what) {
      in >> std::ws;
      std::getline(in, c.text);
      while (!c.text.empty() && (c.text.back() == ' ' || c.text.back() == '\r' || c.text.back() == '\t')) c.text.pop_back();
      if (c.text.empty()) {
        fail("'" + cmd + "' needs " + what);
        return false;
      }
      return true;
    };

    bool good = false;
    if (cmd == "click") { c.kind = ScriptCommand::Kind::Click; good = coords(); }
    else if (cmd == "click-window") { c.kind = ScriptCommand::Kind::ClickWindow; good = coords(); }
    else if (cmd == "move-window") { c.kind = ScriptCommand::Kind::MoveWindow; good = coords(); }
    else if (cmd == "down-window") { c.kind = ScriptCommand::Kind::DownWindow; good = coords(); }
    else if (cmd == "up-window") { c.kind = ScriptCommand::Kind::UpWindow; good = coords(); }
    else if (cmd == "move") { c.kind = ScriptCommand::Kind::Move; good = coords(); }
    else if (cmd == "down") { c.kind = ScriptCommand::Kind::Down; good = coords(); }
    else if (cmd == "up") { c.kind = ScriptCommand::Kind::Up; good = coords(); }
    else if (cmd == "key") { c.kind = ScriptCommand::Kind::Key; 
      good = rest("a key name");
      if (good && !platform::isValidKeySpec(c.text)) {
        fail("unknown key '" + c.text + "'");
        good = false;
      }
    }
    else if (cmd == "type") { c.kind = ScriptCommand::Kind::Type; good = rest("the text to type"); }
    else if (cmd == "lua") { c.kind = ScriptCommand::Kind::Lua; good = rest("Lua code"); }
    else if (cmd == "screenshot-logical") { c.kind = ScriptCommand::Kind::ScreenshotLogical; good = rest("a path"); }
    else if (cmd == "screenshot") { c.kind = ScriptCommand::Kind::Screenshot; good = rest("a path"); }
    else if (cmd == "expect-window") {
      c.kind = ScriptCommand::Kind::ExpectWindow;
      c.ms = 5000;
      good = rest("a window name");
      // "expect-window <name> [timeout_ms]": a trailing all-digit word after the name is the timeout.
      if (good) {
        auto sp = c.text.find_last_of(" \t");
        if (sp != std::string::npos) {
          std::string tail = c.text.substr(sp + 1);
          if (!tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos) {
            c.ms = std::stod(tail);
            c.text.erase(sp);
            while (!c.text.empty() && (c.text.back() == ' ' || c.text.back() == '\t')) c.text.pop_back();
          }
        }
      }
    }
    else if (cmd == "expect-sprite") {
      // "expect-sprite <name-prefix> [min-count] [timeout_ms]": waits until that many visible sprites match.
      c.kind = ScriptCommand::Kind::ExpectSprite;
      c.ms = 5000;
      good = static_cast<bool>(in >> c.text);
      if (!good) {
        fail("'expect-sprite' needs: expect-sprite <name-prefix> [min-count] [timeout_ms]");
      } else {
        double v;
        if (in >> v) {
          c.count = static_cast<int>(v);
          if (in >> v) c.ms = v;
        }
        good = noTrailing(in) && c.count >= 0 && c.ms >= 0;
        if (!good) fail("'expect-sprite' needs: expect-sprite <name-prefix> [min-count] [timeout_ms]");
      }
    }
    else if (cmd == "expect-lua") {
      // "expect-lua <timeout_ms> <expr>": waits until the Lua expression is truthy on the main state.
      c.kind = ScriptCommand::Kind::ExpectLua;
      good = readNumber(in, c.ms) && c.ms >= 0;
      if (good) good = rest("a Lua expression");
      if (!good) fail("'expect-lua' needs: expect-lua <timeout_ms> <expression>");
    }
    else if (cmd == "expect-dump") {
      // "expect-dump <timeout_ms> <text>": waits until the window dump (`dump`) contains the text, e.g. `state=5`.
      c.kind = ScriptCommand::Kind::ExpectDump;
      good = readNumber(in, c.ms) && c.ms >= 0;
      if (good) good = rest("the text to find in the dump");
      if (!good) fail("'expect-dump' needs: expect-dump <timeout_ms> <text>");
    }
    else if (cmd == "autoshoot") {
      // "autoshoot <ms>": for the next ms of clock, every frame where a factory game has a good target, move, press and
      // release there. Runs in the background: the commands after it carry on.
      c.kind = ScriptCommand::Kind::Autoshoot;
      good = readNumber(in, c.ms) && c.ms >= 0 && noTrailing(in);
      if (!good) fail("'autoshoot' needs: autoshoot <ms>");
    }
    else if (cmd == "focus") {
      // "focus lost|gained": the OS telling the app it lost or regained the foreground.
      c.kind = ScriptCommand::Kind::Focus;
      std::string what;
      in >> what;
      good = (what == "lost" || what == "gained") && noTrailing(in);
      c.flag = what == "gained";
      if (!good) fail("'focus' needs: focus lost|gained");
    }
    else if (cmd == "resize") {
      // "resize W H": the window is dragged to W x H window pixels (x, y carry the size).
      c.kind = ScriptCommand::Kind::Resize;
      double w = 0, h = 0;
      good = readNumber(in, w) && readNumber(in, h) && noTrailing(in) && w >= 1 && h >= 1 && w <= 16384 && h <= 16384 &&
             w == static_cast<int>(w) && h == static_cast<int>(h);
      c.x = static_cast<float>(w);
      c.y = static_cast<float>(h);
      if (!good) fail("'resize' needs: resize W H (whole window pixels)");
    }
    else if (cmd == "dump") {
      c.kind = ScriptCommand::Kind::Dump;
      good = noTrailing(in);
      if (!good) fail("'dump' takes no arguments");
    }
    else if (cmd == "diag") {
      c.kind = ScriptCommand::Kind::Diag;
      good = noTrailing(in);
      if (!good) fail("'diag' takes no arguments");
    }
    else if (cmd == "expect-label") {
      // "expect-label <window> <text>": the window's label (GetLabel) must equal the rest of the line, checked at once.
      c.kind = ScriptCommand::Kind::ExpectLabel;
      in >> c.text;
      in >> std::ws;
      std::getline(in, c.text2);
      while (!c.text2.empty() && (c.text2.back() == ' ' || c.text2.back() == '\r' || c.text2.back() == '\t')) c.text2.pop_back();
      good = !c.text.empty();
      if (!good) fail("'expect-label' needs: expect-label <window> <text>");
    }
    else if (cmd == "wait") {
      c.kind = ScriptCommand::Kind::Wait;
      good = readNumber(in, c.ms) && c.ms >= 0 && noTrailing(in);
      if (!good) fail("'wait' needs: wait <ms>");
    } else if (cmd == "quit") {
      c.kind = ScriptCommand::Kind::Quit;
      good = noTrailing(in);
      if (!good) fail("'quit' takes no arguments");
    } else {
      fail("unknown command '" + cmd + "'");
    }
    if (good) out.commands.push_back(std::move(c));
  }
  return out;
}

ScriptDriver::ScriptDriver(std::vector<ScriptCommand> commands, platform::InputInjector& input)
    : commands_(std::move(commands)), input_(input) {}

void ScriptDriver::failIfPending(const char* reason) {
  if (expectingDump_) {
    expectingDump_ = false;
    failed_ = true;
    diag::out("[script] FAIL expect-dump %s (%s)\n", expectName_.c_str(), reason);
    std::fflush(stdout);
    return;
  }
  if (expectingLua_) {
    expectingLua_ = false;
    failed_ = true;
    diag::out("[script] FAIL expect-lua %s (%s)\n", expectName_.c_str(), reason);
    std::fflush(stdout);
    return;
  }
  if (expectingSprite_) {
    expectingSprite_ = false;
    failed_ = true;
    diag::out("[script] FAIL expect-sprite %s %d (%s)\n", expectName_.c_str(), expectCount_, reason);
    std::fflush(stdout);
    return;
  }
  if (!expecting_) return;
  expecting_ = false;
  failed_ = true;
  diag::out("[script] FAIL expect-window %s (%s)\n", expectName_.c_str(), reason);
  std::fflush(stdout);
}

void ScriptDriver::update(double nowMs) {
  ++frames_;
  if (autoshootUntilMs_ >= 0) {
    if (nowMs >= autoshootUntilMs_) {
      autoshootUntilMs_ = -1;
    } else if (windows_) {
      if (auto p = windows_->autoAim()) {
        input_.injectMouseMove(p->x, p->y);
        input_.injectMouseButton(p->x, p->y, true);
        input_.injectMouseButton(p->x, p->y, false);
      }
    }
  }
  if (pendingScreenshot_) return;
  if (expectingDump_) {
    if ((windows_ || extraDump_) && fullDump().find(expectName_) != std::string::npos) {
      expectingDump_ = false;
      diag::out("[script] ok expect-dump %s (frame %llu)\n", expectName_.c_str(), static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
    } else if (nowMs >= expectDeadlineMs_) {
      failIfPending("not in the dump after the timeout");
      return;
    } else {
      return;
    }
  }
  if (expectingLua_) {
    if (luaEval_ && luaEval_(expectName_)) {
      expectingLua_ = false;
      diag::out("[script] ok expect-lua %s (frame %llu)\n", expectName_.c_str(), static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
    } else if (nowMs >= expectDeadlineMs_) {
      failIfPending("false after the timeout");
      return;
    } else {
      return;
    }
  }
  if (expectingSprite_) {
    const int n = windows_ ? windows_->countSprites(expectName_) : 0;
    if (n >= expectCount_) {
      expectingSprite_ = false;
      diag::out("[script] ok expect-sprite %s %d (found %d, frame %llu)\n", expectName_.c_str(), expectCount_, n,
                   static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
    } else if (nowMs >= expectDeadlineMs_) {
      expectingSprite_ = false;
      diag::out("[script] FAIL expect-sprite %s %d (found %d after the timeout, frame %llu)\n", expectName_.c_str(),
                   expectCount_, n, static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
      failed_ = true;
      return;
    } else {
      return;
    }
  }
  if (expecting_) {
    if (windows_->hasScriptError(expectName_)) {
      failIfPending("its script logged a Lua error");
      return;
    }
    if (windows_->isShown(expectName_)) {
      expecting_ = false;
      diag::out("[script] ok expect-window %s (frame %llu)\n", expectName_.c_str(),
                   static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
    } else if (nowMs >= expectDeadlineMs_) {
      expecting_ = false;
      diag::out("[script] FAIL expect-window %s (not shown after the timeout, frame %llu)\n",
                   expectName_.c_str(), static_cast<unsigned long long>(frames_));
      std::fflush(stdout);
      failed_ = true;
      return;
    } else {
      return;
    }
  }
  if (waitUntil_ >= 0) {
    if (nowMs < waitUntil_) return;
    waitUntil_ = -1;
  }
  while (next_ < commands_.size()) {
    const ScriptCommand& c = commands_[next_++];
    using K = ScriptCommand::Kind;
    switch (c.kind) {
      case K::Click:
        input_.injectMouseMove(c.x, c.y);
        input_.injectMouseButton(c.x, c.y, true);
        input_.injectMouseButton(c.x, c.y, false);
        break;
      case K::ClickWindow:
        input_.injectWindowMouseMove(c.x, c.y);
        input_.injectWindowMouseButton(c.x, c.y, true);
        input_.injectWindowMouseButton(c.x, c.y, false);
        break;
      case K::MoveWindow: input_.injectWindowMouseMove(c.x, c.y); break;
      case K::DownWindow: input_.injectWindowMouseButton(c.x, c.y, true); break;
      case K::UpWindow: input_.injectWindowMouseButton(c.x, c.y, false); break;
      case K::Move: input_.injectMouseMove(c.x, c.y); break;
      case K::Down: input_.injectMouseButton(c.x, c.y, true); break;
      case K::Up: input_.injectMouseButton(c.x, c.y, false); break;
      case K::Focus:
        input_.injectForeground(c.flag);
        return;  // end the frame: the event is handled with the next poll
      case K::Resize:
        input_.injectResize(static_cast<int>(c.x), static_cast<int>(c.y));
        return;  // end the frame: the next command sees the new size
      case K::Key:
        if (!input_.injectKey(c.text)) {
          diag::err("script line %d: unknown key '%s'\n", c.line, c.text.c_str());
          failed_ = true;
          return;
        }
        return;  // end the frame: a fullscreen key changes how later input is mapped
      case K::Type:
        input_.injectText(c.text);
        return;  // the text is delivered with this frame's events
      case K::Lua:
        if (!luaRunner_) {
          diag::err("script line %d: 'lua' needs a script host\n", c.line);
          failed_ = true;
          return;
        }
        luaRunner_(c.text);
        return;  // the code may open or close windows: let the frame run
      case K::Wait:
        waitUntil_ = nowMs + c.ms;
        return;
      case K::Screenshot:
      case K::ScreenshotLogical:
        screenshotPath_ = c.text;
        screenshotLogical_ = c.kind == K::ScreenshotLogical;
        pendingScreenshot_ = true;
        return;
      case K::ExpectWindow:
        if (!windows_) {
          diag::out("[script] FAIL expect-window %s (no script host, there are no windows)\n", c.text.c_str());
          std::fflush(stdout);
          failed_ = true;
          return;
        }
        if (windows_->hasScriptError(c.text)) {
          diag::out("[script] FAIL expect-window %s (its script logged a Lua error)\n", c.text.c_str());
          std::fflush(stdout);
          failed_ = true;
          return;
        }
        if (windows_->isShown(c.text)) {
          diag::out("[script] ok expect-window %s (frame %llu)\n", c.text.c_str(),
                       static_cast<unsigned long long>(frames_));
          std::fflush(stdout);
          break;
        }
        expecting_ = true;
        expectName_ = c.text;
        expectDeadlineMs_ = nowMs + c.ms;
        return;
      case K::ExpectSprite: {
        expectName_ = c.text;
        expectCount_ = c.count;
        const int n = windows_ ? windows_->countSprites(c.text) : 0;
        if (n >= c.count) {
          diag::out("[script] ok expect-sprite %s %d (found %d, frame %llu)\n", c.text.c_str(), c.count, n,
                       static_cast<unsigned long long>(frames_));
          std::fflush(stdout);
          break;
        }
        expectingSprite_ = true;
        expectDeadlineMs_ = nowMs + c.ms;
        return;
      }
      case K::ExpectLua:
        if (!luaEval_) {
          diag::out("[script] FAIL expect-lua %s (no script host)\n", c.text.c_str());
          std::fflush(stdout);
          failed_ = true;
          return;
        }
        if (luaEval_(c.text)) {
          diag::out("[script] ok expect-lua %s (frame %llu)\n", c.text.c_str(), static_cast<unsigned long long>(frames_));
          std::fflush(stdout);
          break;
        }
        expectingLua_ = true;
        expectName_ = c.text;
        expectDeadlineMs_ = nowMs + c.ms;
        return;
      case K::Autoshoot: autoshootUntilMs_ = nowMs + c.ms; break;
      case K::ExpectDump:
        if ((windows_ || extraDump_) && fullDump().find(c.text) != std::string::npos) {
          diag::out("[script] ok expect-dump %s (frame %llu)\n", c.text.c_str(), static_cast<unsigned long long>(frames_));
          std::fflush(stdout);
          break;
        }
        expectingDump_ = true;
        expectName_ = c.text;
        expectDeadlineMs_ = nowMs + c.ms;
        return;
      case K::Dump: {
        const std::string text = fullDump();
        diag::outText(text.empty() ? "[script] dump: there are no windows\n" : text);
        std::fflush(stdout);
        break;
      }
      case K::Diag:
        if (diagnostics_) diagnostics_();
        else diag::outText("[script] diag: there is no script host\n");
        break;
      case K::ExpectLabel: {
        std::string got;
        const bool found = windows_ && windows_->labelOf(c.text, got);
        const bool ok = found && got == c.text2;
        diag::out("[script] %s expect-label %s \"%s\"%s\n", ok ? "ok" : "FAIL", c.text.c_str(), c.text2.c_str(),
                     ok ? "" : (found ? (" (the label is \"" + got + "\")").c_str() : " (no such window with a label)"));
        std::fflush(stdout);
        if (!ok) {
          failed_ = true;
          return;
        }
        break;
      }
      case K::Quit:
        quitRequested_ = true;
        next_ = commands_.size();
        return;
    }
  }
}

}  // namespace choco::app
