#include "gfx/SheetCache.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#include "gfx/AnmSheet.h"

namespace choco::gfx {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool endsWith(const std::string &s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         lower(s.substr(s.size() - suffix.size())) == suffix;
}

} // namespace

void SheetCache::logOnce(const std::string &key, const std::string &line) {
  if (logged_.insert(key).second)
    std::fprintf(stderr, "%s\n", line.c_str());
}

std::shared_ptr<const SpriteSheet> SheetCache::get(std::string_view name,
                                                   std::string_view image,
                                                   std::string_view mask) {
  if (name.empty())
    return nullptr;
  static constexpr std::array<std::string_view, 2> kSheetExtensions = {".xml", ".anm"};
  auto path = fs_.probe(name, kSheetExtensions);
  if (!path)
    return nullptr;
  const bool anm = endsWith(*path, ".anm");
  if (!anm && !endsWith(*path, ".xml"))
    return nullptr; // a plain image given by its full name: not a sheet

  const std::string key =
      lower(*path) + "|" + std::string(image) + "|" + std::string(mask);
  if (auto it = entries_.find(key); it != entries_.end())
    return it->second;

  SheetData data;
  std::string error;
  if (anm) {
    // A compiled `.anm` sheet (the SDK's binary form of the XML sheet): the same data the XML carries.
    auto bytes = fs_.readFile(*path);
    std::string_view raw =
        bytes ? std::string_view(reinterpret_cast<const char *>(bytes->data()),
                                 bytes->size())
              : std::string_view();
    if (!bytes || !gfx::AnmSheet::parse(raw, data, error)) {
      logOnce(key, "[sheet] '" + *path + "': " +
                       (bytes ? error : std::string("cannot read the file")));
      return nullptr;
    }
  } else {
    auto text = fs_.readText(*path);
    if (!text || !SpriteSheet::parse(*text, data, error)) {
      logOnce(key, "[sheet] '" + *path + "': " +
                       (text ? error : std::string("cannot read the file")));
      return nullptr;
    }
  }
  for (const std::string &w : data.warnings)
    logOnce(key + w, "[sheet] '" + *path + "': " + w);

  const std::size_t slash = path->find_last_of('/');
  const std::string dir =
      slash == std::string::npos ? std::string() : path->substr(0, slash + 1);
  auto locate = [&](const std::string &file) -> std::string {
    if (file.empty())
      return {};
    if (fs_.exists(dir + file))
      return dir + file;
    return file;
  };
  const std::string imageName =
      locate(image.empty() ? data.image : std::string(image));
  const std::string maskName =
      locate(mask.empty() ? data.mask : std::string(mask));

  auto sheet = std::make_shared<SpriteSheet>();
  sheet->path = *path;
  sheet->image = textures_.load(imageName, maskName);
  if (!sheet->image) {
    logOnce(key, "[sheet] '" + *path + "': image '" + imageName +
                     "' could not be loaded");
    return nullptr;
  }
  sheet->frames = std::move(data.frames);
  sheet->script = std::move(data.script);
  sheet->reg = data.reg;
  entries_[key] = sheet;
  return sheet;
}

void SheetCache::trim() {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.use_count() == 1)
      it = entries_.erase(it);
    else
      ++it;
  }
}

} // namespace choco::gfx
