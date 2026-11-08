#include "assets/AssetSource.h"

#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "platform/PathUtil.h"

namespace choco::assets {

namespace fs = std::filesystem;

void DirectorySource::list(std::vector<std::string>& out) const {
  std::error_code ec;
  const fs::path root = platform::pathFromUtf8(root_);
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    // The original skips Subversion metadata when scanning.
    if (it->is_directory(ec) && it->path().filename() == ".svn") {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    const std::u8string rel = it->path().lexically_relative(root).generic_u8string();
    out.emplace_back(rel.begin(), rel.end());
  }
}

void ShallowDirectorySource::list(std::vector<std::string>& out) const {
  std::error_code ec;
  const fs::path root = platform::pathFromUtf8(root_);
  auto lowered = [](std::string n) {
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n;
  };
  auto scan = [&](const fs::path& dir) {
    std::error_code e2;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, e2), end; !e2 && it != end;
         it.increment(e2)) {
      if (!it->is_regular_file(e2)) continue;
      const std::u8string rel = it->path().lexically_relative(root).generic_u8string();
      out.emplace_back(rel.begin(), rel.end());
    }
  };
  scan(root);
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    const std::string name = lowered(platform::pathToUtf8(it->path().filename()));
    for (const std::string& want : subdirs_)
      if (name == want) scan(it->path());
  }
}

std::optional<Bytes> ShallowDirectorySource::read(const std::string& relPath) const {
  return readWholeFile(platform::pathFromUtf8(root_) / platform::pathFromUtf8(relPath));
}

std::optional<Bytes> DirectorySource::read(const std::string& relPath) const {
  return readWholeFile(platform::pathFromUtf8(root_) / platform::pathFromUtf8(relPath));
}

std::optional<Bytes> readWholeFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  const std::streamsize n = f.tellg();
  if (n < 0) return std::nullopt;
  Bytes data(static_cast<std::size_t>(n));
  f.seekg(0);
  if (n > 0 && !f.read(reinterpret_cast<char*>(data.data()), n)) return std::nullopt;
  return data;
}

}  // namespace choco::assets

namespace choco::assets {

std::unique_ptr<PfpSource> PfpSource::fromBytes(Bytes data, std::string& error) {
  auto u32 = [&](std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | static_cast<std::uint32_t>(data[at + 1]) << 8 |
           static_cast<std::uint32_t>(data[at + 2]) << 16 | static_cast<std::uint32_t>(data[at + 3]) << 24;
  };
  if (data.size() < 8 || std::memcmp(data.data(), "PFPK", 4) != 0) {
    error = "not a PFPK archive";
    return nullptr;
  }
  const std::uint32_t count = u32(4);
  std::unique_ptr<PfpSource> src(new PfpSource());
  std::size_t pos = 8;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (pos >= data.size() || pos + 1 + static_cast<std::size_t>(std::to_integer<unsigned>(data[pos])) + 8 > data.size()) {
      error = "archive directory is truncated at entry " + std::to_string(i);
      return nullptr;
    }
    const std::size_t len = std::to_integer<unsigned>(data[pos]);
    Entry e;
    e.name.assign(reinterpret_cast<const char*>(data.data()) + pos + 1, len);
    e.offset = u32(pos + 1 + len);
    e.size = u32(pos + 5 + len);
    pos += 9 + len;
    if (e.offset > data.size() || e.size > data.size() - e.offset) {
      error = "entry '" + e.name + "' lies outside the archive";
      return nullptr;
    }
    src->byName_.emplace(e.name, src->entries_.size());
    src->entries_.push_back(std::move(e));
  }
  src->data_ = std::move(data);
  return src;
}

std::unique_ptr<PfpSource> PfpSource::openIndexOnly(const fs::path& path, std::string& error) {
  std::error_code ec;
  const std::uintmax_t total = fs::file_size(path, ec);
  std::ifstream f(path, std::ios::binary);
  if (ec || !f) {
    error = "cannot read archive " + platform::pathToUtf8(path);
    return nullptr;
  }
  auto u32 = [](const unsigned char* b) {
    return static_cast<std::uint32_t>(b[0]) | static_cast<std::uint32_t>(b[1]) << 8 |
           static_cast<std::uint32_t>(b[2]) << 16 | static_cast<std::uint32_t>(b[3]) << 24;
  };
  unsigned char head[8];
  if (!f.read(reinterpret_cast<char*>(head), 8) || std::memcmp(head, "PFPK", 4) != 0) {
    error = "not a PFPK archive";
    return nullptr;
  }
  const std::uint32_t count = u32(head + 4);
  std::unique_ptr<PfpSource> src(new PfpSource());
  src->lazyPath_ = path;
  for (std::uint32_t i = 0; i < count; ++i) {
    unsigned char len = 0;
    unsigned char nums[8];
    if (!f.read(reinterpret_cast<char*>(&len), 1)) {
      error = "archive directory is truncated at entry " + std::to_string(i);
      return nullptr;
    }
    Entry e;
    e.name.resize(len);
    if ((len && !f.read(e.name.data(), len)) || !f.read(reinterpret_cast<char*>(nums), 8)) {
      error = "archive directory is truncated at entry " + std::to_string(i);
      return nullptr;
    }
    e.offset = u32(nums);
    e.size = u32(nums + 4);
    if (e.offset > total || e.size > total - e.offset) {
      error = "entry '" + e.name + "' lies outside the archive";
      return nullptr;
    }
    src->byName_.emplace(e.name, src->entries_.size());
    src->entries_.push_back(std::move(e));
  }
  return src;
}

std::unique_ptr<PfpSource> PfpSource::open(const fs::path& path, std::string& error) {
  auto bytes = readWholeFile(path);
  if (!bytes) {
    error = "cannot read archive " + platform::pathToUtf8(path);
    return nullptr;
  }
  return fromBytes(std::move(*bytes), error);
}

void PfpSource::list(std::vector<std::string>& out) const {
  for (const Entry& e : entries_) out.push_back(e.name);
}

std::optional<Bytes> PfpSource::read(const std::string& relPath) const {
  auto it = byName_.find(relPath);
  if (it == byName_.end()) return std::nullopt;
  const Entry& e = entries_[it->second];
  if (!lazyPath_.empty()) {
    std::ifstream f(lazyPath_, std::ios::binary);
    Bytes out(e.size);
    f.seekg(static_cast<std::streamoff>(e.offset));
    if (!f || (e.size > 0 && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(e.size))))
      return std::nullopt;
    return out;
  }
  return Bytes(data_.begin() + static_cast<std::ptrdiff_t>(e.offset),
               data_.begin() + static_cast<std::ptrdiff_t>(e.offset + e.size));
}

namespace {

std::uint32_t rd32(const std::byte* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// Reads `size` bytes at `offset` from an open file; false if short.
bool readAt(std::ifstream& f, std::uint64_t offset, std::byte* dst, std::size_t size) {
  f.clear();
  f.seekg(static_cast<std::streamoff>(offset));
  if (size == 0) return static_cast<bool>(f);
  return static_cast<bool>(f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size)));
}

}  // namespace

std::uint32_t CadSource::hashPath(std::string_view s) {
  auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(s[i])); };
  std::uint32_t h = static_cast<std::uint32_t>(s.size());
  const std::size_t rem = s.size() & 3, words = s.size() >> 2;
  std::size_t p = 0;
  for (std::size_t i = 0; i < words; ++i, p += 4) {
    h += at(p) | at(p + 1) << 8;
    h = (h << 16) ^ ((at(p + 2) | at(p + 3) << 8) << 11) ^ h;
    h += h >> 11;
  }
  // Tail bytes are signed chars in the original.
  auto sc = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(s[i]))); };
  if (rem == 3) {
    h += at(p) | at(p + 1) << 8;
    h ^= h << 16;
    h ^= sc(p + 2) << 18;
    h += h >> 11;
  } else if (rem == 2) {
    h += at(p) | at(p + 1) << 8;
    h ^= h << 11;
    h += h >> 17;
  } else if (rem == 1) {
    h += sc(p);
    h ^= h << 10;
    h += h >> 1;
  }
  h ^= h << 3;
  h += h >> 5;
  h ^= h << 4;
  h += h >> 17;
  h ^= h << 25;
  h += h >> 6;
  return h;
}

std::unique_ptr<CadSource> CadSource::open(const fs::path& path, std::string& error) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    error = "cannot read CAD file " + platform::pathToUtf8(path);
    return nullptr;
  }
  const std::uint64_t fileSize = static_cast<std::uint64_t>(f.tellg());
  std::byte head[0x20];
  if (fileSize < sizeof head || !readAt(f, 0, head, sizeof head)) {
    error = "CAD file is too small for a header";
    return nullptr;
  }
  const std::uint32_t symKey = rd32(head + 8), count = rd32(head + 12);
  if (count == 0 || 0x20 + static_cast<std::uint64_t>(count) * 16 > fileSize) {
    error = "CAD directory of " + std::to_string(count) + " entries does not fit the file";
    return nullptr;
  }
  Bytes dir(static_cast<std::size_t>(count) * 16);
  if (!readAt(f, 0x20, dir.data(), dir.size())) {
    error = "CAD directory is truncated";
    return nullptr;
  }
  struct Raw {
    std::uint32_t key, size, offset;
  };
  std::vector<Raw> raw(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::byte* e = dir.data() + static_cast<std::size_t>(i) * 16;
    if (rd32(e + 4) != 0) {
      error = "CAD entry " + std::to_string(i) + " has a key above 32 bits";
      return nullptr;
    }
    raw[i] = {rd32(e), rd32(e + 8), rd32(e + 12)};
    if (raw[i].offset < 0x20 + static_cast<std::uint64_t>(count) * 16 || raw[i].offset + std::uint64_t{raw[i].size} > fileSize) {
      error = "CAD entry " + std::to_string(i) + " lies outside the file";
      return nullptr;
    }
  }
  // The symbol table: <Key>authoring path</Key> records; hash each path to find its directory entry.
  std::unordered_map<std::uint32_t, std::string> pathByKey;
  bool sawSymbols = false;
  for (const Raw& r : raw) {
    if (r.key != symKey) continue;
    sawSymbols = true;
    std::string xml(r.size, '\0');
    if (!readAt(f, r.offset, reinterpret_cast<std::byte*>(xml.data()), xml.size())) {
      error = "CAD symbol table is truncated";
      return nullptr;
    }
    for (std::size_t at = 0; (at = xml.find("<Key>", at)) != std::string::npos;) {
      at += 5;
      const std::size_t end = xml.find("</Key>", at);
      if (end == std::string::npos) break;
      pathByKey.emplace(hashPath(std::string_view(xml).substr(at, end - at)), xml.substr(at, end - at));
      at = end;
    }
    break;
  }

  if (!sawSymbols) {
    error = "CAD has no symbol-table entry (key 0x" + [&] {
      char buf[16];
      std::snprintf(buf, sizeof buf, "%08x", symKey);
      return std::string(buf);
    }() + ")";
    return nullptr;
  }

  std::unique_ptr<CadSource> src(new CadSource());
  src->path_ = path;
  src->entryCount_ = count;
  struct Named {
    std::string path;
    std::uint32_t offset, size;
  };
  std::vector<Named> named;
  for (const Raw& r : raw) {
    std::byte magic[4] = {};
    if (r.size < 4 || !readAt(f, r.offset, magic, 4) || std::memcmp(magic, "OggS", 4) != 0) continue;
    ++src->oggCount_;
    auto it = pathByKey.find(r.key);
    if (it == pathByKey.end()) {
      ++src->unnamedOgg_;
      continue;
    }
    src->oggs_.push_back({r.key, it->second, r.size});
    named.push_back({it->second, r.offset, r.size});
  }
  if (named.empty()) {
    error = "CAD symbol table names none of its Ogg entries";
    return nullptr;
  }

  auto serve = [&](std::string name, const Named& n) {
    if (src->byName_.emplace(name, src->served_.size()).second) src->served_.push_back({std::move(name), n.offset, n.size});
  };
  auto sameBytes = [&](const Named& a, const Named& b) {
    if (a.size != b.size) return false;
    Bytes x(a.size), y(b.size);
    return readAt(f, a.offset, x.data(), x.size()) && readAt(f, b.offset, y.data(), y.size()) && x == y;
  };
  // Reforged extracted the second copy of two duplicated basenames under other names; vanilla's project XML uses both
  // copies (the port music channels use the second Ce_MapMELODICLAYER). Keyed by lowercase basename.
  static const std::pair<const char*, const char*> kReforgedAlias[] = {
      {"ce_mapmelodiclayer.ogg", "Ce_MapMELODICLAYER (2).ogg"},
      {"footsteps.ogg", "footsteps_2.ogg"},
  };
  std::unordered_map<std::string, std::size_t> firstOfBase;  // lowercase basename -> index in `named`
  for (std::size_t i = 0; i < named.size(); ++i) {
    const Named& n = named[i];
    std::string slashed = n.path;
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    serve("cadi/cad/" + slashed, n);
    const std::size_t slash = slashed.find_last_of('/');
    const std::string base = slashed.substr(slash == std::string::npos ? 0 : slash + 1);
    std::string lowBase = base;
    for (char& c : lowBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto [it, fresh] = firstOfBase.emplace(lowBase, i);
    if (fresh) {
      serve("cadi/audio/" + base, n);
      continue;
    }
    if (sameBytes(named[it->second], n)) continue;
    for (const auto& [from, alias] : kReforgedAlias)
      if (lowBase == from) serve(std::string("cadi/audio/") + alias, n);
  }
  return src;
}

void CadSource::list(std::vector<std::string>& out) const {
  for (const Entry& e : served_) out.push_back(e.name);
}

std::optional<Bytes> CadSource::read(const std::string& relPath) const {
  auto it = byName_.find(relPath);
  if (it == byName_.end()) return std::nullopt;
  const Entry& e = served_[it->second];
  std::ifstream f(path_, std::ios::binary);
  Bytes data(e.size);
  if (!f || !readAt(f, e.offset, data.data(), data.size())) return std::nullopt;
  return data;
}

}  // namespace choco::assets
