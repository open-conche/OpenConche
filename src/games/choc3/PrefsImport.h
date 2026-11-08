#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/Users.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// Import of the original game's prefs.dat (docs/re/prefs-and-saves.md): a Blowfish-ECB decryptor, a tolerant chunk parser and
// a mapper onto UserStore. No SDL, no file access except through the caller: the functions take the file's bytes, so the
// source file is only ever read, by the caller, and never written.

// Textbook Blowfish (16 rounds, big-endian words, ECB). The P array and the four S-boxes are the hexadecimal digits of pi,
// computed once at first use (Machin's formula on a big fixed-point number), not embedded.
class Blowfish {
 public:
  // Key of 1..56 bytes, cycled over the P array.
  Blowfish(const std::uint8_t* key, std::size_t keyLen);
  void encryptBlock(std::uint32_t& left, std::uint32_t& right) const;
  void decryptBlock(std::uint32_t& left, std::uint32_t& right) const;

 private:
  std::uint32_t f(std::uint32_t x) const;
  std::array<std::uint32_t, 18> p_{};
  std::array<std::array<std::uint32_t, 256>, 4> s_{};
};

// The word tables (18 P words, then S0..S3 of 256 words each) straight from pi; exposed for the tests.
const std::vector<std::uint32_t>& blowfishPiWords();

// TEncrypt with base64 off: PKCS-like padding (1..8 bytes, each holding the count) and Blowfish ECB with the game's fixed key.
class PrefsCipher {
 public:
  PrefsCipher();  // key "H32kwmdbCAAA" base64-decoded to 9 bytes
  std::string encrypt(std::string_view plain) const;
  // TBlowfish::DecryptString: a length that is not a multiple of 8 is zero-extended; nullopt where the original returns size 0
  // (nothing, or an impossible pad count).
  std::optional<std::string> decrypt(std::string_view blob) const;

 private:
  Blowfish bf_;
};

// The 9 key bytes the game derives from its key string (for the tests).
std::vector<std::uint8_t> prefsKeyBytes();

// Result of reading the chunk container. `sections[0]` is the global set, `sections[i + 1]` user i, for the identity of the
// file's first IKEY chunk. Values are the raw bytes up to the first NUL.
struct ParsedPrefs {
  std::string identity;
  std::vector<std::map<std::string, std::string>> sections;
  std::size_t chunks = 0;
  bool complete = true;  // false when parsing stopped early at a bad or truncated chunk (everything before it is kept)
  std::size_t stoppedAt = 0;
  std::string stopReason;
  std::vector<std::string> otherIdentities;  // further IKEY sets in the file (other games), ignored
};

struct PrefsParseResult {
  bool ok = false;  // false: nothing usable (empty, not a prefs.dat, or the first chunk is bad); `error` says why
  std::string error;
  ParsedPrefs prefs;
};

PrefsParseResult parsePrefsDat(std::string_view data);

// Test/tool writer in the layout SavePrefs produces: IKEY(first), then IKEY and USER/KVPR chunks per section.
std::string buildPrefsDat(const std::string& identity, const std::vector<std::map<std::string, std::string>>& sections);

// What the import did for one user of the file, so the caller can verify it against the disk afterwards.
struct ImportedUser {
  int fileIndex = 0;
  int storeIndex = -1;
  std::string name;
  bool replaced = false;      // overwrote an existing user of the same name
  bool hadOldSave = false;    // the replaced user had an engine save before
  bool saveWritten = false;   // the imported save was written
  std::string save;           // that save (empty unless saveWritten)
  std::string saveNote;       // "imported", "kept: ...", "none"
};

struct ImportReport {
  int usersFound = 0;
  int usersImported = 0;
  int usersReplaced = 0;  // imported over an existing user of the same name
  int savesImported = 0;
  int savesRefused = 0;  // the never-initialised Player fingerprint
  int savesEmpty = 0;    // users with no savegame pref
  int prefsMapped = 0;
  int currentUser = -1;  // the store's current index after the import
  std::vector<ImportedUser> users;
  bool writeFailed = false;  // a store write reported failure
  std::vector<std::string> ignored;   // "user 1: foo" / "global: _pflifetime": prefs the engine does not use
  std::vector<std::string> warnings;  // renamed users, truncation, bad values
};

// Writes the users, per-user prefs, saves, the global fullscreen pref and the current user into `store`. A user whose name
// already exists in the store before the import is overwritten (prefs and save); the others are appended.
ImportReport importPrefs(const ParsedPrefs& prefs, UserStore& store);

// Human-readable summary lines for the CLI.
std::string formatReport(const ImportReport& r);

}  // namespace choco::game
