#pragma once

#include <array>
#include <filesystem>
#include <span>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/AssetSource.h"

namespace choco::assets {

// Extension orders used by the original, see docs/re/startup-and-assets.md section 3.
// (std::array, not initializer_list: GCC 11 rejects a constexpr initializer_list at namespace scope.)
inline constexpr std::array<std::string_view, 8> kAssetExtensions = {".xml", ".anm", ".jpg", ".png",
                                                                     ".spx", ".ogg", ".mesh", ".lua"};
inline constexpr std::array<std::string_view, 3> kTextureExtensions = {".png", ".jpg", ".jp2"};

// Lowercased, '/'-separated, case-insensitive view of the game's files. The index of the assets root is built once at
// construction. Names accept '\\'. Two prefixes bypass the index:
//  - "user:" is resolved against the user directory on every call (nothing is cached, nothing is created until a write);
//  - "::" means a real path relative to the working directory. Only if that does not exist and the name starts with
//    "assets/" is the rest looked up in the assets index (so "::assets/settings.xml" works from any cwd).
class AssetFS {
 public:
  // assetsRoot: the --assets directory. userDir: the "user:" root (may be empty for none); not touched until writeFile.
  // Probe: just enough to recognise a game (the finder's detection) - the root and a few known folders instead of
  // the whole tree, the archive's index without its data, no audio containers. Everything a game looks up at
  // detection time (settings.xml, the marker and main-loop scripts) is found; it is not for running a game.
  enum class Mode { Full, Probe };
  AssetFS(const std::string& assetsRoot, const std::string& userDir, Mode mode = Mode::Full);

  bool ok() const { return error_.empty(); }
  const std::string& error() const { return error_; }

  // Adds a source to the assets index. Earlier sources win on equal paths.
  void addSource(std::unique_ptr<AssetSource> source);
  void mountCad(const std::string& assetsRoot);

  // Writes a whole file below the user directory (creating parent directories now, not before). `path` must be a
  // "user:" path; returns false otherwise or on an I/O error.
  bool writeFile(std::string_view path, const Bytes& data);
  bool writeText(std::string_view path, std::string_view text);

  // Canonical lookup key: normalised, lowercased. Empty if the name is not a valid asset name.
  static std::string normalize(std::string_view path);

  bool exists(std::string_view path) const;
  // The real-case relative path, or for "user:" the lowercased name, or for "::" the name as given; nullopt if missing.
  std::optional<std::string> resolve(std::string_view path) const;
  std::optional<Bytes> readFile(std::string_view path) const;
  std::optional<std::string> readText(std::string_view path) const;

  // Exact name if it exists, otherwise base + each extension in order. Returns the resolved path.
  std::optional<std::string> probe(std::string_view base, std::span<const std::string_view> extensions = kAssetExtensions) const;

  // Every resolved path whose lowercased key ends with `suffix` (lowercased by the caller or here), sorted by key.
  std::vector<std::string> listBySuffix(std::string_view suffix) const;

  // The files directly inside the directory `dir` (not its subdirectories), as real-case relative paths, sorted bytewise by
  // lowercased key: TFile::GetNextFile's order (docs/re/factories-questions.md 7). `dir` may end in '/'. Empty for an
  // unknown directory.
  std::vector<std::string> list(std::string_view dir) const;

  std::size_t fileCount() const { return index_.size(); }
  const std::string& assetsRoot() const { return assetsRoot_; }
  const std::string& userRoot() const { return userDir_; }
  // The app learns the game (and so its default user folder) only after the assets are mounted.
  void setUserRoot(std::string dir) { userDir_ = std::move(dir); }

 private:
  struct Entry {
    std::size_t source;
    std::string rel;  // path inside the source, real case
  };
  const Entry* find(std::string_view key) const;
  // The disk path for a "user:" or "::" name when it exists; empty otherwise. `isUser` tells which kind it was.
  std::filesystem::path diskPath(std::string_view path, bool& special) const;

  std::string assetsRoot_, userDir_, error_;
  std::vector<std::unique_ptr<AssetSource>> sources_;
  std::map<std::string, Entry> index_;
};

}  // namespace choco::assets
