#include "games/choc3/PrefsImport.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

#include "games/choc3/PlayerSave.h"

namespace choco::game {

namespace {

// ---- pi, as hexadecimal words: arctan series on a big-endian fixed-point number -----------------------------------------

// Limb 0 is the integer part, limbs 1.. the fraction, most significant first.
struct Big {
  explicit Big(std::size_t n) : w(n, 0) {}
  bool zero() const {
    for (std::uint32_t x : w)
      if (x != 0) return false;
    return true;
  }
  void divSmall(std::uint32_t d) {
    std::uint64_t rem = 0;
    for (std::uint32_t& x : w) {
      const std::uint64_t cur = (rem << 32) | x;
      x = static_cast<std::uint32_t>(cur / d);
      rem = cur % d;
    }
  }
  void mulSmall(std::uint32_t m) {
    std::uint64_t carry = 0;
    for (std::size_t i = w.size(); i-- > 0;) {
      const std::uint64_t cur = static_cast<std::uint64_t>(w[i]) * m + carry;
      w[i] = static_cast<std::uint32_t>(cur);
      carry = cur >> 32;
    }
  }
  void add(const Big& o) {
    std::uint64_t carry = 0;
    for (std::size_t i = w.size(); i-- > 0;) {
      const std::uint64_t cur = static_cast<std::uint64_t>(w[i]) + o.w[i] + carry;
      w[i] = static_cast<std::uint32_t>(cur);
      carry = cur >> 32;
    }
  }
  void sub(const Big& o) {
    std::uint64_t borrow = 0;
    for (std::size_t i = w.size(); i-- > 0;) {
      const std::uint64_t take = static_cast<std::uint64_t>(o.w[i]) + borrow;
      borrow = w[i] < take ? 1 : 0;
      w[i] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(w[i]) - take);
    }
  }
  std::vector<std::uint32_t> w;
};

// arctan(1/x) for a small integer x.
Big arctanInv(std::uint32_t x, std::size_t limbs) {
  Big total(limbs);
  total.w[0] = 1;
  total.divSmall(x);
  Big term = total;
  const std::uint32_t x2 = x * x;
  std::uint32_t n = 1;
  bool negative = true;
  while (!term.zero()) {
    term.divSmall(x2);
    n += 2;
    Big t = term;
    t.divSmall(n);
    if (negative) total.sub(t);
    else total.add(t);
    negative = !negative;
  }
  return total;
}

std::vector<std::uint32_t> computePiWords(std::size_t count) {
  const std::size_t limbs = count + 5;  // integer limb + count fraction limbs + 4 guard limbs
  Big a = arctanInv(5, limbs);
  a.mulSmall(4);
  Big b = arctanInv(239, limbs);
  a.sub(b);
  a.mulSmall(4);  // pi
  return std::vector<std::uint32_t>(a.w.begin() + 1, a.w.begin() + 1 + static_cast<std::ptrdiff_t>(count));
}

std::vector<std::uint8_t> base64Decode(std::string_view s) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<std::uint8_t> out;
  std::uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    const int v = val(c);
    if (v < 0) continue;
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

std::uint32_t loadBE(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void storeBE(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}

constexpr std::uint32_t kMaxChunk = 0x400000;
constexpr const char* kKeyBase64 = "H32kwmdbCAAA";

std::string cstr(std::string_view v) {
  const std::size_t nul = v.find('\0');
  return std::string(nul == std::string_view::npos ? v : v.substr(0, nul));
}

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::uint32_t readLE32(std::string_view d, std::size_t pos) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(d[pos])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d[pos + 1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d[pos + 2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d[pos + 3])) << 24);
}

void appendLE32(std::string& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

}  // namespace

// ---- Blowfish ------------------------------------------------------------------------------------------------------------

const std::vector<std::uint32_t>& blowfishPiWords() {
  static const std::vector<std::uint32_t> words = computePiWords(18 + 4 * 256);
  return words;
}

Blowfish::Blowfish(const std::uint8_t* key, std::size_t keyLen) {
  const std::vector<std::uint32_t>& pi = blowfishPiWords();
  for (std::size_t i = 0; i < 18; ++i) p_[i] = pi[i];
  for (std::size_t b = 0; b < 4; ++b)
    for (std::size_t i = 0; i < 256; ++i) s_[b][i] = pi[18 + 256 * b + i];
  static const std::uint8_t kZero = 0;
  if (keyLen == 0) {  // an empty key acts as one zero byte
    key = &kZero;
    keyLen = 1;
  }
  std::size_t j = 0;
  for (std::size_t i = 0; i < 18; ++i) {
    std::uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      v = (v << 8) | key[j];
      j = (j + 1) % keyLen;
    }
    p_[i] ^= v;
  }
  std::uint32_t l = 0, r = 0;
  for (std::size_t i = 0; i < 18; i += 2) {
    encryptBlock(l, r);
    p_[i] = l;
    p_[i + 1] = r;
  }
  for (std::size_t b = 0; b < 4; ++b)
    for (std::size_t i = 0; i < 256; i += 2) {
      encryptBlock(l, r);
      s_[b][i] = l;
      s_[b][i + 1] = r;
    }
}

std::uint32_t Blowfish::f(std::uint32_t x) const {
  std::uint32_t h = s_[0][x >> 24] + s_[1][(x >> 16) & 0xFF];
  h ^= s_[2][(x >> 8) & 0xFF];
  return h + s_[3][x & 0xFF];
}

void Blowfish::encryptBlock(std::uint32_t& left, std::uint32_t& right) const {
  std::uint32_t l = left, r = right;
  for (std::size_t i = 0; i < 16; ++i) {
    l ^= p_[i];
    r ^= f(l);
    std::swap(l, r);
  }
  std::swap(l, r);
  r ^= p_[16];
  l ^= p_[17];
  left = l;
  right = r;
}

void Blowfish::decryptBlock(std::uint32_t& left, std::uint32_t& right) const {
  std::uint32_t l = left, r = right;
  for (std::size_t i = 17; i > 1; --i) {
    l ^= p_[i];
    r ^= f(l);
    std::swap(l, r);
  }
  std::swap(l, r);
  r ^= p_[1];
  l ^= p_[0];
  left = l;
  right = r;
}

std::vector<std::uint8_t> prefsKeyBytes() {
  std::vector<std::uint8_t> k = base64Decode(kKeyBase64);
  k.resize(9);  // Base64DecodeGetLength(12)
  return k;
}

PrefsCipher::PrefsCipher() : bf_(prefsKeyBytes().data(), 9) {}

std::string PrefsCipher::encrypt(std::string_view plain) const {
  const std::size_t pad = 8 - (plain.size() & 7);
  std::string buf(plain);
  buf.append(pad, static_cast<char>(pad));
  for (std::size_t i = 0; i < buf.size(); i += 8) {
    std::uint8_t* p = reinterpret_cast<std::uint8_t*>(&buf[i]);
    std::uint32_t l = loadBE(p), r = loadBE(p + 4);
    bf_.encryptBlock(l, r);
    storeBE(p, l);
    storeBE(p + 4, r);
  }
  return buf;
}

std::optional<std::string> PrefsCipher::decrypt(std::string_view blob) const {
  std::string buf(blob);
  if (buf.size() % 8) buf.append(8 - buf.size() % 8, '\0');
  if (buf.empty()) return std::nullopt;
  for (std::size_t i = 0; i < buf.size(); i += 8) {
    std::uint8_t* p = reinterpret_cast<std::uint8_t*>(&buf[i]);
    std::uint32_t l = loadBE(p), r = loadBE(p + 4);
    bf_.decryptBlock(l, r);
    storeBE(p, l);
    storeBE(p + 4, r);
  }
  const std::size_t pad = static_cast<unsigned char>(buf.back());
  if (pad > buf.size()) return std::nullopt;
  buf.resize(buf.size() - pad);
  return buf;
}

// ---- container -----------------------------------------------------------------------------------------------------------

PrefsParseResult parsePrefsDat(std::string_view data) {
  PrefsParseResult res;
  ParsedPrefs& p = res.prefs;
  if (data.empty()) {
    res.error = "the file is empty";
    return res;
  }
  if (data.size() >= 2 && data[0] == '<' && data[1] == '>') {
    res.error = "this is an old XML prefs file (LoadXMLPrefs), which is not supported";
    return res;
  }
  static const PrefsCipher cipher;
  std::size_t pos = 0;
  std::string current;  // identity the following chunks belong to
  bool haveIdentity = false;
  std::set<std::string> others;
  std::uint32_t curUser = 0;
  auto stop = [&](std::size_t at, const std::string& why) {
    p.complete = false;
    p.stoppedAt = at;
    p.stopReason = why;
  };
  while (pos < data.size()) {
    const std::size_t start = pos;
    if (data.size() - pos < 8) {
      stop(start, "a truncated chunk header (" + std::to_string(data.size() - pos) + " trailing bytes)");
      break;
    }
    const std::string_view tag = data.substr(pos, 4);
    const std::uint32_t n = readLE32(data, pos + 4);
    if (n == 0 || n > kMaxChunk) {
      stop(start, "a chunk with an impossible length (" + std::to_string(n) + ")");
      break;
    }
    if (data.size() - pos - 8 < n) {
      stop(start, "a truncated chunk (needs " + std::to_string(n) + " bytes, " + std::to_string(data.size() - pos - 8) +
                      " remain)");
      break;
    }
    std::string body(data.substr(pos + 8, n));
    pos += 8 + n;
    const char kind = tag[0];
    if (!haveIdentity && kind != 'I') {
      stop(start, "a first chunk that is not IKEY");
      break;
    }
    if (kind != 'U') {
      std::optional<std::string> plain = cipher.decrypt(body);
      if (!plain || plain->empty()) {
        stop(start, "a chunk that does not decrypt (wrong key or damaged padding)");
        break;
      }
      body = std::move(*plain);
    }
    if (kind == 'I') {
      current = lower(cstr(body));
      if (!haveIdentity) {
        haveIdentity = true;
        p.identity = current;
        p.sections.assign(1, {});
      } else if (current != p.identity) {
        others.insert(current);
      }
    } else if (kind == 'U') {
      if (body.size() < 2) {
        stop(start, "a USER chunk shorter than 2 bytes");
        break;
      }
      curUser = static_cast<std::uint32_t>(static_cast<unsigned char>(body[0])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(body[1])) << 8);
      if (curUser > 0x400) {
        stop(start, "a USER index above 1024");
        break;
      }
      if (current == p.identity)
        while (p.sections.size() <= curUser) p.sections.emplace_back();
    } else if (kind == 'K') {
      if (body.size() < 2) {
        stop(start, "a KVPR chunk shorter than 2 bytes");
        break;
      }
      const std::size_t klen = static_cast<std::size_t>(static_cast<unsigned char>(body[0])) |
                               (static_cast<std::size_t>(static_cast<unsigned char>(body[1])) << 8);
      if (body.size() < klen + 2) {
        stop(start, "a KVPR chunk whose key is longer than the chunk");
        break;
      }
      if (current == p.identity) {
        while (p.sections.size() <= curUser) p.sections.emplace_back();
        p.sections[curUser][cstr(std::string_view(body).substr(2, klen))] = cstr(std::string_view(body).substr(2 + klen));
      }
    } else {
      stop(start, std::string("an unknown chunk tag '") + std::string(tag) + "'");
      break;
    }
    ++p.chunks;
  }
  p.otherIdentities.assign(others.begin(), others.end());
  if (!haveIdentity) {
    res.error = "not a Chocolatier prefs.dat: " + (p.stopReason.empty() ? std::string("no IKEY chunk") : p.stopReason);
    return res;
  }
  res.ok = true;
  return res;
}

std::string buildPrefsDat(const std::string& identity, const std::vector<std::map<std::string, std::string>>& sections) {
  static const PrefsCipher cipher;
  std::string out;
  auto chunk = [&](const char* tag, const std::string& payload, bool encrypted) {
    const std::string body = encrypted ? cipher.encrypt(payload) : payload;
    out += tag;
    appendLE32(out, static_cast<std::uint32_t>(body.size()));
    out += body;
  };
  const std::string ident = identity + std::string(1, '\0');
  chunk("IKEY", ident, true);
  chunk("IKEY", ident, true);
  for (std::size_t i = 0; i < sections.size(); ++i) {
    std::string u;
    u += static_cast<char>(i & 0xFF);
    u += static_cast<char>((i >> 8) & 0xFF);
    chunk("USER", u, false);
    for (const auto& [k, v] : sections[i]) {
      const std::size_t klen = k.size() + 1;
      std::string kv;
      kv += static_cast<char>(klen & 0xFF);
      kv += static_cast<char>((klen >> 8) & 0xFF);
      kv += k;
      kv += '\0';
      kv += v;
      kv += '\0';
      chunk("KVPR", kv, true);
    }
  }
  return out;
}

// ---- mapping onto UserStore ----------------------------------------------------------------------------------------------

namespace {

bool isInteger(const std::string& s) {
  std::size_t i = !s.empty() && s[0] == '-' ? 1 : 0;
  if (i >= s.size() || s.size() - i > 9) return false;
  for (; i < s.size(); ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

void appendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Windows-1252 to UTF-8, for a name the original stored in the Windows ANSI code page. The five undefined bytes (81 8D 8F 90 9D)
// map to the C1 control of the same value.
std::string cp1252ToUtf8(const std::string& s) {
  static const unsigned kHigh[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
  std::string out;
  for (char ch : s) {
    const unsigned c = static_cast<unsigned char>(ch);
    appendUtf8(out, c >= 0x80 && c < 0xA0 ? kHigh[c - 0x80] : c);
  }
  return out;
}

struct SoundDefault {
  const char* key;
  const char* value;
};
constexpr SoundDefault kSoundDefaults[] = {{"soundlevel", "750"}, {"musiclevel", "650"}, {"ambientlevel", "500"}, {"soundoff", "0"}};

bool isSoundKey(const std::string& k) {
  for (const auto& d : kSoundDefaults)
    if (k == d.key) return true;
  return false;
}

}  // namespace

ImportReport importPrefs(const ParsedPrefs& prefs, UserStore& store) {
  ImportReport rep;
  if (!prefs.complete)
    rep.warnings.push_back("the file stops early at byte " + std::to_string(prefs.stoppedAt) + " (" + prefs.stopReason +
                           "); everything before that was imported");
  {
    std::string found = "'" + prefs.identity + "'";
    for (const auto& o : prefs.otherIdentities) found += ", '" + o + "'";
    if (prefs.identity != "chocolatier-decadence-design" || !prefs.otherIdentities.empty())
      rep.warnings.push_back("identities in the file: " + found + "; only the first ('" + prefs.identity +
                             "') was read, and the game's own is 'chocolatier-decadence-design'");
  }
  const std::map<std::string, std::string> kNoPrefs;
  const auto& global = prefs.sections.empty() ? kNoPrefs : prefs.sections[0];
  const int userCount = prefs.sections.empty() ? 0 : static_cast<int>(prefs.sections.size()) - 1;
  rep.usersFound = userCount;

  const int preCount = store.count();
  std::vector<bool> claimed(static_cast<std::size_t>(preCount), false);
  std::vector<int> storeIndex(static_cast<std::size_t>(userCount), -1);

  for (int i = 0; i < userCount; ++i) {
    const auto& sec = prefs.sections[static_cast<std::size_t>(i) + 1];
    const std::string tag = "user " + std::to_string(i);
    std::string name;
    if (auto it = sec.find("name"); it != sec.end()) name = it->second;
    if (!UserStore::validName(name)) {
      const std::string conv = cp1252ToUtf8(name);
      if (!name.empty() && UserStore::validName(conv)) {
        rep.warnings.push_back(tag + ": name is not UTF-8, read as Windows-1252 ('" + conv + "')");
        name = conv;
      } else {
        name = "Player " + std::to_string(i + 1);
        rep.warnings.push_back(tag + ": no usable name, called '" + name + "'");
      }
    }
    ImportedUser iu;
    iu.fileIndex = i;
    iu.name = name;
    int idx = -1;
    for (int e = 0; e < preCount; ++e)
      if (!claimed[static_cast<std::size_t>(e)] && store.name(e) == name) {
        idx = e;
        claimed[static_cast<std::size_t>(e)] = true;
        ++rep.usersReplaced;
        iu.replaced = true;
        store.setCurrent(e);
        iu.hadOldSave = !store.loadSave().empty();
        break;
      }
    if (idx < 0) idx = store.create(name);
    if (idx < 0) {
      rep.warnings.push_back(tag + ": could not create '" + name + "', skipped");
      continue;
    }
    if (!store.setCurrent(idx)) rep.writeFailed = true;
    storeIndex[static_cast<std::size_t>(i)] = idx;
    iu.storeIndex = idx;
    ++rep.usersImported;

    for (const auto& d : kSoundDefaults)
      if (sec.find(d.key) == sec.end()) store.setPref(d.key, d.value);  // create() copies the previous user's; use the original's defaults
    for (const auto& [k, v] : sec) {
      if (k == "name" || k == "savegame") continue;
      if (isSoundKey(k)) {
        if (!isInteger(v)) {
          rep.warnings.push_back(tag + ": " + k + " is not a number ('" + v + "'), ignored");
          continue;
        }
        store.setPref(k, v);
        ++rep.prefsMapped;
      } else if (k == "_anonusername") {
        store.setPref(k, v);
        ++rep.prefsMapped;
      } else {
        rep.ignored.push_back(tag + ": " + k);
      }
    }

    auto sv = sec.find("savegame");
    if (sv == sec.end() || sv->second.empty()) {
      ++rep.savesEmpty;
      iu.saveNote = iu.hadOldSave ? "no save in the file; the existing engine save is kept" : "no save";
    } else if (isUninitialisedPlayerSave(sv->second)) {
      ++rep.savesRefused;
      iu.saveNote = iu.hadOldSave ? "save refused (never-initialised player); the existing engine save is kept" : "save refused (never-initialised player)";
      rep.warnings.push_back(tag + " (" + name + "): save refused, it is the never-initialised Player fingerprint");
    } else if (store.saveSave(sv->second)) {
      ++rep.savesImported;
      iu.saveWritten = true;
      iu.save = sv->second;
      iu.saveNote = iu.hadOldSave ? "save overwritten" : "save imported";
    } else {
      rep.writeFailed = true;
      iu.saveNote = "the save could not be written";
      rep.warnings.push_back(tag + " (" + name + "): the save could not be written");
    }
    rep.users.push_back(std::move(iu));
  }

  for (const auto& [k, v] : global) {
    if (k == "curuser") continue;
    if (k == "fullscreen") {
      if (v == "0" || v == "1") {
        store.setGlobalPref(k, v);
        ++rep.prefsMapped;
      } else {
        rep.warnings.push_back("global: fullscreen is not 0 or 1 ('" + v + "'), ignored");
      }
    } else {
      rep.ignored.push_back("global: " + k);
    }
  }

  int cur = -1;
  if (auto it = global.find("curuser"); it != global.end()) {
    if (isInteger(it->second)) cur = std::atoi(it->second.c_str());
    else rep.warnings.push_back("global: curuser is not a number ('" + it->second + "')");
  }
  if (cur >= 0 && cur < userCount && storeIndex[static_cast<std::size_t>(cur)] >= 0) {
    store.setCurrent(storeIndex[static_cast<std::size_t>(cur)]);
  } else {
    if (cur >= userCount) rep.warnings.push_back("global: curuser " + std::to_string(cur) + " is out of range, no current user");
    store.setCurrent(-1);
  }
  rep.currentUser = store.current();
  return rep;
}

std::string formatReport(const ImportReport& r) {
  std::string s;
  s += "users found:      " + std::to_string(r.usersFound) + "\n";
  s += "users imported:   " + std::to_string(r.usersImported) + " (" + std::to_string(r.usersReplaced) + " replaced an existing user of the same name)\n";
  s += "saves imported:   " + std::to_string(r.savesImported) + "\n";
  s += "saves refused:    " + std::to_string(r.savesRefused) + " (never-initialised player)\n";
  s += "users with no save: " + std::to_string(r.savesEmpty) + "\n";
  s += "prefs mapped:     " + std::to_string(r.prefsMapped) + "\n";
  s += "prefs ignored:    " + std::to_string(r.ignored.size()) + "\n";
  for (const auto& i : r.ignored) s += "  ignored " + i + "\n";
  for (const auto& u : r.users)
    s += "  " + u.name + ": " + (u.replaced ? "replaced the existing user" : "new user") + "; " + u.saveNote + "\n";
  for (const auto& w : r.warnings) s += "warning: " + w + "\n";
  s += "current user:     " + std::to_string(r.currentUser) + "\n";
  return s;
}

}  // namespace choco::game
