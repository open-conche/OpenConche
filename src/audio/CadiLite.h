#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "audio/EventTable.h"

namespace choco::audio {

// The Macintosh Chocolatier 3 audio spec (docs/design/mac-audio.md): a text file of event blocks, each a list of
// `"other"` calls, `play <group> <file> [loop]` and `stop <file>` entries.

struct CadiLiteEntry {
  enum class Type { Call, Play, Stop } type = Type::Play;
  std::string name;   // Call: the callee
  std::string group;  // Play: Sfx, Music, Environments, or any other word
  std::string file;   // Play, Stop: the file text as written (without the "cadi/" prefix)
  bool loop = false;  // Play
  int line = 0;
};

struct CadiLiteBlock {
  std::string name;
  int line = 0;  // the line of the `:"name"` header
  std::vector<CadiLiteEntry> entries;
};

struct CadiLiteSpec {
  std::vector<CadiLiteBlock> blocks;      // every block that is kept, in file order
  std::vector<std::string> messages;      // "[audio] cadilite.spec:<line>: <text>"
  int errors = 0;                         // malformed lines and bad calls (each skipped)
  int duplicates = 0;                     // blocks dropped because an earlier block has the same name
};

// Reads the text. A malformed line is reported in `messages` and skipped; loading goes on.
CadiLiteSpec parseCadiLite(std::string_view text);

struct CadiLiteCompiled {
  std::vector<EventDef> events;  // one per kept block, in file order
  std::vector<std::string> messages;  // the parse messages, then the compile warnings
  int errors = 0;
  int duplicates = 0;
  int unknown = 0;  // events compiled to Kind::Unknown
};

// Parses and compiles the text into the event rows of docs/design/mac-audio.md s.2.
CadiLiteCompiled compileCadiLite(std::string_view text);

}  // namespace choco::audio
