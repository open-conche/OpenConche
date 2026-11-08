#include "assets/AssetFS.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

#include "platform/PathUtil.h"
#include "platform/UserDir.h"

namespace choco::assets {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
  std::string r(s);
  // ASCII only: matches the original's byte-wise tolower and keeps UTF-8 intact.
  for (char& c : r)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return r;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

std::string slashes(std::string_view path) {
  std::string s(path);
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

bool isFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

}  // namespace

AssetFS::AssetFS(const std::string& assetsRoot, const std::string& userDir, Mode mode)
    : assetsRoot_(assetsRoot), userDir_(userDir) {
  std::error_code ec;
  if (!fs::is_directory(platform::pathFromUtf8(assetsRoot), ec)) {
    error_ = "assets directory '" + assetsRoot + "' does not exist or is not a directory";
    return;
  }
  // Precedence: loose files first, then the archive. Sources added earlier win, so a loose file in the assets directory
  // overrides an `assets.pfp` entry with the same (case-insensitive) path.
  const bool probe = mode == Mode::Probe;
  if (probe) addSource(std::make_unique<ShallowDirectorySource>(
                 assetsRoot, std::vector<std::string>{"data", "ui", "community", "scripts"}));
  else addSource(std::make_unique<DirectorySource>(assetsRoot));
  const fs::path pfp = platform::pathFromUtf8(assetsRoot) / "assets.pfp";
  if (isFile(pfp)) {
    std::string err;
    if (auto src = probe ? PfpSource::openIndexOnly(pfp, err) : PfpSource::open(pfp, err)) addSource(std::move(src));
    else error_ = "assets.pfp: " + err;
  }
  if (!probe) {
    mountCad(assetsRoot);
  }
}

// Vanilla ships its audio only inside `cadi/*.CAD`; Reforged ships it extracted as `cadi/audio/`. Serve the container's
// Oggs under that path only when the directory is absent, and after the loose files so those still win.
void AssetFS::mountCad(const std::string& assetsRoot) {
  std::error_code ec;
  const fs::path cadi = platform::pathFromUtf8(assetsRoot) / "cadi";
  if (!fs::is_directory(cadi, ec) || fs::is_directory(cadi / "audio", ec)) return;
  std::vector<fs::path> cads;
  for (fs::directory_iterator it(cadi, ec), end; !ec && it != end; it.increment(ec)) {
    std::string ext = platform::pathToUtf8(it->path().extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".cad" && it->is_regular_file(ec)) cads.push_back(it->path());
  }
  std::sort(cads.begin(), cads.end());
  for (const fs::path& p : cads) {
    std::string err;
    if (auto src = CadSource::open(p, err)) addSource(std::move(src));
    else std::fprintf(stderr, "warning: ignoring CAD %s: %s\n", platform::pathToUtf8(p.filename()).c_str(), err.c_str());  // audio is optional
  }
}

void AssetFS::addSource(std::unique_ptr<AssetSource> source) {
  std::vector<std::string> files;
  source->list(files);
  const std::size_t id = sources_.size();
  sources_.push_back(std::move(source));
  for (auto& f : files) {
    std::string key = normalize(f);
    if (key.empty()) continue;
    index_.emplace(std::move(key), Entry{id, std::move(f)});
  }
}

std::string AssetFS::normalize(std::string_view path) {
  std::string s = lower(slashes(path));
  // A "::" or "user:" prefix is kept as written; ".." is rejected, except after "::" (a real path).
  std::string prefix;
  bool allowDotDot = false;
  if (startsWith(s, "::")) {
    prefix = "::";
    allowDotDot = true;
  } else if (startsWith(s, "user:")) {
    prefix = "user:";
  }
  s.erase(0, prefix.size());
  // Split into segments, dropping "" and "."; "user:/x" and "user:x" both mean the user root.
  std::string out;
  std::size_t i = 0;
  while (i <= s.size()) {
    std::size_t j = s.find('/', i);
    if (j == std::string::npos) j = s.size();
    std::string_view seg(s.data() + i, j - i);
    if (seg == ".." && !allowDotDot) return {};
    if (!seg.empty() && seg != ".") {
      if (!out.empty()) out += '/';
      out += seg;
    }
    i = j + 1;
  }
  return prefix + out;
}

const AssetFS::Entry* AssetFS::find(std::string_view key) const {
  auto it = index_.find(std::string(key));
  return it == index_.end() ? nullptr : &it->second;
}

// "user:" -> userDir/<lowercased rest>, the original builds folder + "/" + rest per call. "::" -> a real path.
fs::path AssetFS::diskPath(std::string_view path, bool& special) const {
  special = false;
  const std::string key = normalize(path);
  if (startsWith(key, "user:")) {
    special = true;
    if (userDir_.empty() || key.size() == 5) return {};
    const fs::path p = platform::pathFromUtf8(userDir_) / platform::pathFromUtf8(key.substr(5));
    return isFile(p) ? p : fs::path();
  }
  if (startsWith(key, "::")) {
    special = true;
    const std::string given = slashes(path).substr(slashes(path).find("::") + 2);
    if (given.empty()) return {};
    for (const std::string& cand : {given, key.substr(2)}) {
      const fs::path p = platform::pathFromUtf8(cand);
      if (isFile(p)) return p;
    }
  }
  return {};
}

bool AssetFS::exists(std::string_view path) const { return resolve(path).has_value(); }

std::optional<std::string> AssetFS::resolve(std::string_view path) const {
  const std::string key = normalize(path);
  if (key.empty()) return std::nullopt;
  bool special = false;
  const fs::path disk = diskPath(path, special);
  if (special) {
    if (!disk.empty()) return startsWith(key, "user:") ? key : slashes(path);
    // "::assets/x" falls back to the assets index when no such file exists relative to the cwd.
    if (startsWith(key, "::assets/")) {
      if (const Entry* e = find(key.substr(9))) return e->rel;
    }
    return std::nullopt;
  }
  if (const Entry* e = find(key)) return e->rel;
  return std::nullopt;
}

std::optional<Bytes> AssetFS::readFile(std::string_view path) const {
  const std::string key = normalize(path);
  if (key.empty()) return std::nullopt;
  bool special = false;
  const fs::path disk = diskPath(path, special);
  if (special) {
    if (!disk.empty()) return readWholeFile(disk);
    if (startsWith(key, "::assets/")) {
      if (const Entry* e = find(key.substr(9))) return sources_[e->source]->read(e->rel);
    }
    return std::nullopt;
  }
  const Entry* e = find(key);
  return e ? sources_[e->source]->read(e->rel) : std::nullopt;
}

std::optional<std::string> AssetFS::readText(std::string_view path) const {
  auto b = readFile(path);
  if (!b) return std::nullopt;
  return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

bool AssetFS::writeFile(std::string_view path, const Bytes& data) {
  const std::string key = normalize(path);
  if (!startsWith(key, "user:") || key.size() == 5 || userDir_.empty()) return false;
  const fs::path p = platform::pathFromUtf8(userDir_) / platform::pathFromUtf8(key.substr(5));
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  if (ec) return false;
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!f.flush()) return false;
  f.close();
  platform::syncUserDir();
  return true;
}

bool AssetFS::writeText(std::string_view path, std::string_view text) {
  Bytes b(text.size());
  std::transform(text.begin(), text.end(), b.begin(), [](char c) { return static_cast<std::byte>(c); });
  return writeFile(path, b);
}

std::optional<std::string> AssetFS::probe(std::string_view base, std::span<const std::string_view> extensions) const {
  if (auto r = resolve(base)) return r;
  for (std::string_view ext : extensions) {
    std::string name(base);
    name += ext;
    if (auto r = resolve(name)) return r;
  }
  return std::nullopt;
}

std::vector<std::string> AssetFS::listBySuffix(std::string_view suffix) const {
  const std::string suf = lower(suffix);
  std::vector<std::string> out;
  for (const auto& [key, e] : index_) {  // map order is sorted by key
    if (key.size() >= suf.size() && key.compare(key.size() - suf.size(), suf.size(), suf) == 0) out.push_back(e.rel);
  }
  return out;
}

std::vector<std::string> AssetFS::list(std::string_view dir) const {
  std::string prefix = normalize(dir);
  std::vector<std::string> out;
  if (prefix.empty()) return out;
  if (prefix.back() != '/') prefix += '/';
  for (auto it = index_.lower_bound(prefix); it != index_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
    if (it->first.find('/', prefix.size()) == std::string::npos) out.push_back(it->second.rel);
  }
  return out;
}

}  // namespace choco::assets
