#include "app/Diagnostics.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

#include "platform/PathUtil.h"
#include "script/GameModule.h"
#include "script/ScriptContext.h"
#include "script/ScriptHost.h"
#include "ui/WindowManager.h"

namespace choco::diag {

namespace {

std::ofstream g_file;
bool g_logMotion = false;
std::chrono::steady_clock::time_point g_start;

std::string vformat(const char* fmt, va_list ap) {
  va_list copy;
  va_copy(copy, ap);
  const int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  if (n <= 0) return {};
  std::vector<char> buf(static_cast<std::size_t>(n) + 1);
  std::vsnprintf(buf.data(), buf.size(), fmt, ap);
  return std::string(buf.data(), static_cast<std::size_t>(n));
}

}  // namespace

bool openLog(const std::string& path, std::string& error) {
  closeLog();
  g_file.open(platform::pathFromUtf8(path), std::ios::out | std::ios::trunc | std::ios::binary);
  if (!g_file) {
    error = "cannot open log file '" + path + "'";
    return false;
  }
  g_start = std::chrono::steady_clock::now();
  const long long epoch =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  logLine("[log] opened, unix time " + std::to_string(epoch) + "; timestamps are ms since this line");
  return true;
}

bool logOpen() { return g_file.is_open(); }

void closeLog() {
  if (g_file.is_open()) g_file.close();
}

void logLine(const std::string& text) {
  if (!g_file.is_open()) return;
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_start).count();
  char prefix[32];
  std::snprintf(prefix, sizeof prefix, "[+%lldms] ", ms);
  std::size_t pos = 0;
  bool first = true;
  while (pos < text.size() || first) {
    first = false;
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    g_file << prefix << line << '\n';
    pos = nl + 1;
  }
  g_file.flush();
}

void out(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string s = vformat(fmt, ap);
  va_end(ap);
  std::fputs(s.c_str(), stdout);
  std::fflush(stdout);
  logLine(s);
}

void err(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string s = vformat(fmt, ap);
  va_end(ap);
  std::fputs(s.c_str(), stderr);
  logLine(s);
}

void setLogMotion(bool on) { g_logMotion = on; }

void logInput(bool motion, const char* fmt, ...) {
  if (motion && !g_logMotion) return;
  va_list ap;
  va_start(ap, fmt);
  const std::string s = vformat(fmt, ap);
  va_end(ap);
  logLine(s);
}

void outText(const std::string& text) {
  std::fputs(text.c_str(), stdout);
  std::fflush(stdout);
  logLine(text);
}

namespace {
bool gForeground = true;
int gBackgroundDelayMs = 0;
}  // namespace

void setAppState(bool foreground, int backgroundDelayMs) {
  gForeground = foreground;
  gBackgroundDelayMs = backgroundDelayMs;
}

std::string buildReport(script::ScriptContext* ctx, double platformNowMs, long frame) {
  char buf[320];
  std::string r;
  std::snprintf(buf, sizeof buf, "engine clock: platform now %.1f ms, frame %ld", platformNowMs, frame);
  r += buf;
  if (!ctx) return r + "\n(no script host)\n";
  std::snprintf(buf, sizeof buf, "; script clock %.1f ms (frame %llu), window manager clock %.1f ms\n", ctx->clock.nowMs,
                static_cast<unsigned long long>(ctx->clock.frame), ctx->wm.nowMs());
  r += buf;
  r += "app: foreground=" + std::to_string(gForeground ? 1 : 0) + " backgroundDelayMs=" + std::to_string(gBackgroundDelayMs) + "\n";

  if (ctx->host) r += ctx->host->diagnose();
  r += ctx->wm.diagnose();

  if (ctx->module) ctx->module->appendDiagnostics(*ctx, r);

  r += "window tree:\n" + ctx->wm.dump();
  return r;
}

void writeReport(script::ScriptContext* ctx, double platformNowMs, long frame) {
  outText("[diag] ---- BEGIN diagnostics ----\n" + buildReport(ctx, platformNowMs, frame) + "[diag] ---- END diagnostics ----\n");
}

}  // namespace choco::diag
