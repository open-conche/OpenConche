#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace choco::assets {

using Bytes = std::vector<std::byte>;

// Reads a whole file from disk.
std::optional<Bytes> readWholeFile(const std::filesystem::path& p);

// One place files can come from. AssetFS layers sources in order; the first source that has a path wins.
class AssetSource {
 public:
  virtual ~AssetSource() = default;
  // Appends every file as a '/'-separated path relative to the source root, in the source's real case.
  virtual void list(std::vector<std::string>& out) const = 0;
  // Reads a whole file by one of the paths list() returned.
  virtual std::optional<Bytes> read(const std::string& relPath) const = 0;
};

// Files under a directory on disk.
class DirectorySource final : public AssetSource {
 public:
  explicit DirectorySource(std::string rootUtf8) : root_(std::move(rootUtf8)) {}
  void list(std::vector<std::string>& out) const override;
  std::optional<Bytes> read(const std::string& relPath) const override;
  const std::string& root() const { return root_; }

 private:
  std::string root_;
};

// Files directly inside `root` and inside the named subfolders only (no recursion): enough to recognise a game
// without walking its whole tree. The subfolder names match case-insensitively, as the index itself does.
class ShallowDirectorySource final : public AssetSource {
 public:
  ShallowDirectorySource(std::string rootUtf8, std::vector<std::string> subdirs)
      : root_(std::move(rootUtf8)), subdirs_(std::move(subdirs)) {}
  void list(std::vector<std::string>& out) const override;
  std::optional<Bytes> read(const std::string& relPath) const override;

 private:
  std::string root_;
  std::vector<std::string> subdirs_;
};

// The files of a vanilla `assets.pfp`: "PFPK", u32 count, then per file u8 name length, name, u32 offset, u32 size, all
// little-endian; data is stored uncompressed at the given offsets (docs/re/pfp.md). open() reads the whole archive into
// memory once, so read() is a copy and never touches the disk; openIndexOnly() keeps just the directory. Names are kept as stored; AssetFS lowercases them.
class PfpSource final : public AssetSource {
 public:
  // Parses an archive held in memory. On failure returns nullptr and sets `error`.
  static std::unique_ptr<PfpSource> fromBytes(Bytes data, std::string& error);
  // Reads and parses the file at `path`.
  static std::unique_ptr<PfpSource> open(const std::filesystem::path& path, std::string& error);

  // Reads only the directory at the front of the archive; read() then fetches an entry from the file on demand.
  // For recognising a game: the whole archive (15 MB and more) is not loaded.
  static std::unique_ptr<PfpSource> openIndexOnly(const std::filesystem::path& path, std::string& error);

  void list(std::vector<std::string>& out) const override;
  std::optional<Bytes> read(const std::string& relPath) const override;
  std::size_t fileCount() const { return entries_.size(); }

 private:
  struct Entry {
    std::string name;
    std::size_t offset, size;
  };
  PfpSource() = default;
  Bytes data_;
  std::filesystem::path lazyPath_;  // set for an index-only source: entries are read from here
  std::vector<Entry> entries_;
  std::unordered_map<std::string, std::size_t> byName_;  // first entry wins on duplicate names
};

// The Ogg files inside vanilla's CADI container (`cadi/*.CAD`), served as `cadi/audio/<name>.ogg` so the same audio
// event table works for vanilla and Reforged (docs/re/audio.md s.10.9). Layout, little-endian: u32 at 0x08 is the
// key of the symbol-table entry, u32 at 0x0c the entry count, then from 0x20 `count` 16-byte entries (u64 key, u32
// size, u32 absolute offset). A key is the 32-bit SuperFastHash of the resource's authoring path; the symbol table (an
// XML of `<Key>` path strings) maps hashes back to paths, and the basename gives the file name. Only the directory and
// the symbol table are read when mounting; read() seeks into the file and returns one entry's bytes.
// Names served (all case-insensitive once in AssetFS):
//  * `cadi/cad/<authoring path, backslashes as slashes>` for every named Ogg: unique, addresses one exact resource.
//  * `cadi/audio/<basename>`: Reforged-compatible. Where several entries share a basename (13 pairs in the real file)
//    the one earliest in the directory wins, which equals Reforged's file of that name in every case.
//  * `cadi/audio/<Reforged alias>` for the later twin of a basename whose bytes differ, where Reforged extracted that
//    twin under a different name (`Ce_MapMELODICLAYER (2).ogg`, `footsteps_2.ogg`; a small table in the source).
// Mounting fails if the symbol table is missing or names no Ogg.
class CadSource final : public AssetSource {
 public:
  // Parses the directory of the file at `path`. On failure returns nullptr and sets `error`.
  static std::unique_ptr<CadSource> open(const std::filesystem::path& path, std::string& error);

  void list(std::vector<std::string>& out) const override;
  std::optional<Bytes> read(const std::string& relPath) const override;

  // A named Ogg entry, in directory order.
  struct OggInfo {
    std::uint32_t key;
    std::string path;  // authoring path as in the symbol table (backslashes)
    std::uint32_t size;
  };
  const std::vector<OggInfo>& entries() const { return oggs_; }

  std::size_t entryCount() const { return entryCount_; }
  std::size_t oggCount() const { return oggCount_; }
  // Ogg entries for which the symbol table gave no name (not served).
  std::size_t unnamedOggCount() const { return unnamedOgg_; }

  // Paul Hsieh's SuperFastHash over raw bytes, as CadiApi.dll computes it for resource paths.
  static std::uint32_t hashPath(std::string_view s);

 private:
  struct Entry {
    std::string name;  // a served path, e.g. "cadi/audio/<basename>"
    std::uint32_t offset, size;
  };
  CadSource() = default;
  std::filesystem::path path_;
  std::size_t entryCount_ = 0, oggCount_ = 0, unnamedOgg_ = 0;
  std::vector<Entry> served_;
  std::vector<OggInfo> oggs_;
  std::unordered_map<std::string, std::size_t> byName_;
};

}  // namespace choco::assets
