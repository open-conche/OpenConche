#pragma once

// The developer console's command line (docs/re/greatcc-debug-tools.md 1.3): how ShopWindow::DoCommand splits and reads its text. Pure
// functions, no windows and no Lua.

#include <string>
#include <vector>

namespace choco::games::greatcc {

struct DebugCommand {
  std::string word;                // lower-cased; empty for an empty line
  std::vector<std::string> args;   // case kept, empty pieces dropped
};

// The command word is the text up to the first space, lower-cased; the arguments are the runs of non-space characters after it
// (the separator is a single space character only: tabs and commas are not separators).
DebugCommand parseDebugCommand(const std::string& line);

// str::to_int and str::to_float as atoi and atof: the longest numeric prefix, 0 when there is none. Ours: a value out of range is
// clamped and a non-finite float (inf, nan) reads as 0, so no sprite is ever given a NaN position.
int debugToInt(const std::string& s);
float debugToFloat(const std::string& s);

// addminutes: the level clock's elapsed milliseconds moved by `minutes` * 60000. Ours: the result stays within 0..INT_MAX (a negative
// amount only rewinds the clock; the original's field would go negative or wrap).
int debugAddMinutes(int elapsedMs, int minutes);

// VChocolateLayer::GetTypeFromString: bucket 0, ingredient 1, chocolate 2, infusion 3, topping 4 (a `layer` suffix and a `k` prefix
// are allowed, case does not matter); -1 for anything else.
int chocolateLayerTypeFromString(std::string name);

}  // namespace choco::games::greatcc
