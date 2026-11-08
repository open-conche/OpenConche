#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

namespace choco::script {
struct ScriptContext;
}

// Real-play diagnostics: the --log file and the "what is the engine doing" report (F10 or the harness `diag`).
namespace choco::diag {

#if defined(__GNUC__) || defined(__clang__)
#define CHOCO_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#define CHOCO_PRINTF_FORMAT2 __attribute__((format(printf, 2, 3)))
#else
#define CHOCO_PRINTF_FORMAT
#define CHOCO_PRINTF_FORMAT2
#endif

// Opens (truncates) the log file. Every line is then written with a "[+<ms since start>ms]" prefix and flushed at once.
// File logging never touches stdout, so it works in a Windows GUI-subsystem build that has no console.
bool openLog(const std::string& path, std::string& error);
bool logOpen();
void closeLog();

// One line (or several, split at newlines) into the log file only.
void logLine(const std::string& text);

// printf-style: to stdout (or stderr) as before, and into the log file.
void out(const char* fmt, ...) CHOCO_PRINTF_FORMAT;
void err(const char* fmt, ...) CHOCO_PRINTF_FORMAT;
// Already formatted text to stdout and the log file.
void outText(const std::string& text);

// An [input] line: log file only, never the console. Mouse motion (`motion` true) is dropped unless setLogMotion(true).
void setLogMotion(bool on);
void logInput(bool motion, const char* fmt, ...) CHOCO_PRINTF_FORMAT2;

// The app loop's focus state, shown in the report (foreground=, backgroundDelayMs=).
void setAppState(bool foreground, int backgroundDelayMs);

// The full diagnostics report. `ctx` may be null (no script host): then only the engine clock is reported.
std::string buildReport(script::ScriptContext* ctx, double platformNowMs, long frame);
// Builds the report and sends it to stdout and the log file, framed by BEGIN/END lines.
void writeReport(script::ScriptContext* ctx, double platformNowMs, long frame);

}  // namespace choco::diag
