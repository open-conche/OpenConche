#include "gfx/TextureCache.h"
#include "app/Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "gfx/Image.h"

namespace choco::gfx {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

Texture::~Texture() {
  if (handle_ && uploader_) uploader_->release(handle_);
}

TextureCache::Resolved TextureCache::resolve(std::string_view name) const {
  Resolved r;
  std::string_view base = name;
  auto q = name.find('?');
  if (q != std::string_view::npos) {
    base = name.substr(0, q);
    r.options = std::string(name.substr(q + 1));
  }
  if (base.empty()) return r;
  if (auto p = fs_.probe(base, assets::kTextureExtensions)) r.path = *p;
  return r;
}

std::string TextureCache::findMask(const std::string& path, std::string_view explicitMask) const {
  if (!explicitMask.empty()) {
    Resolved m = resolve(explicitMask);
    if (m.path.empty()) diag::err("[gfx] mask '%.*s' not found\n", static_cast<int>(explicitMask.size()), explicitMask.data());
    return m.path;
  }
  const std::string l = lower(path);
  if (l.size() < 4 || l.compare(l.size() - 4, 4, ".jpg") != 0) return {};
  if (auto sibling = fs_.resolve(path.substr(0, path.size() - 4) + ".mask.png")) return *sibling;
  return {};
}

bool TextureCache::loadPixels(const std::string& path, const std::string& maskPath, ImageData& img) const {
  auto bytes = fs_.readFile(path);
  if (!bytes || !decodeImage(*bytes, img)) return false;
  if (maskPath.empty()) return true;
  auto maskBytes = fs_.readFile(maskPath);
  ImageData mask;
  if (!maskBytes || !decodeImage(*maskBytes, mask)) {
    diag::err("[gfx] cannot decode mask '%s'; '%s' stays opaque\n", maskPath.c_str(), path.c_str());
    return true;
  }
  if (mask.width != img.width || mask.height != img.height) {
    diag::err("[gfx] mask '%s' is %dx%d but '%s' is %dx%d; ignoring the mask\n", maskPath.c_str(), mask.width,
                 mask.height, path.c_str(), img.width, img.height);
    return true;
  }
  // The blue channel carries the alpha, except in masks that keep it in the red channel alone (every Chocolatier 1 JPEG mask and
  // the Chocolatier 2 `mask` PNGs: green and blue are 0 or JPEG noise). The later games' masks are grey, so red == blue there and
  // their output is the same either way.
  int maxRed = 0, maxBlue = 0;
  const std::size_t n = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
  for (std::size_t i = 0; i < n; ++i) {
    maxRed = std::max<int>(maxRed, mask.rgba[i * 4]);
    maxBlue = std::max<int>(maxBlue, mask.rgba[i * 4 + 2]);
  }
  const std::size_t channel = (maxBlue < 128 && maxRed >= 128) ? 0 : 2;
  for (std::size_t i = 0; i < n; ++i) img.rgba[i * 4 + 3] = mask.rgba[i * 4 + channel];
  return true;
}

std::shared_ptr<Texture> TextureCache::load(std::string_view name, std::string_view mask) {
  Resolved r = resolve(name);
  if (r.path.empty()) {
    diag::err("[gfx] texture '%.*s' not found\n", static_cast<int>(name.size()), name.data());
    return nullptr;
  }
  const std::string maskPath = findMask(r.path, mask);
  const std::string key = lower(r.path) + (maskPath.empty() ? std::string() : "|" + lower(maskPath));
  if (auto it = entries_.find(key); it != entries_.end()) return it->second;

  ImageData img;
  if (!loadPixels(r.path, maskPath, img)) {
    diag::err("[gfx] cannot decode texture '%s'\n", r.path.c_str());
    return nullptr;
  }
  void* handle = uploader_ ? uploader_->upload(img.rgba.data(), img.width, img.height) : nullptr;
  if (uploader_ && !handle) {
    diag::err("[gfx] cannot upload texture '%s'\n", r.path.c_str());
    return nullptr;
  }
  std::shared_ptr<Texture> tex(new Texture());
  tex->width_ = tex->pixelWidth_ = img.width;
  tex->height_ = tex->pixelHeight_ = img.height;
  tex->path_ = r.path;
  tex->options_ = r.options;
  tex->maskPath_ = maskPath;
  tex->handle_ = handle;
  tex->uploader_ = uploader_;
  entries_[key] = tex;
  return tex;
}

bool TextureCache::loadImage(std::string_view name, ImageData& out) const {
  Resolved r = resolve(name);
  if (r.path.empty()) {
    diag::err("[gfx] image '%.*s' not found\n", static_cast<int>(name.size()), name.data());
    return false;
  }
  if (!loadPixels(r.path, findMask(r.path, {}), out)) {
    diag::err("[gfx] cannot decode image '%s'\n", r.path.c_str());
    return false;
  }
  return true;
}

std::shared_ptr<Texture> TextureCache::makeFromPixels(const std::string& key, const ImageData& img) {
  if (img.width <= 0 || img.height <= 0 || img.rgba.size() < static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4)
    return nullptr;
  if (auto it = entries_.find(key); it != entries_.end()) {
    Texture& tex = *it->second;
    if (tex.width_ == img.width && tex.height_ == img.height && tex.handle_ && uploader_ &&
        uploader_->update(tex.handle_, 0, 0, img.width, img.height, img.rgba.data(), img.width * 4)) {
      memory_[key] = img;
      alphas_.erase(key + "|");  // the pixels changed: a mask derived from them is stale (the edited-in-place logo sign)
      return it->second;
    }
    void* fresh = uploader_ ? uploader_->upload(img.rgba.data(), img.width, img.height) : nullptr;
    if (uploader_ && !fresh) return nullptr;
    if (tex.handle_ && uploader_) uploader_->release(tex.handle_);
    tex.handle_ = fresh;
    tex.width_ = tex.pixelWidth_ = img.width;
    tex.height_ = tex.pixelHeight_ = img.height;
    memory_[key] = img;
    alphas_.erase(key + "|");
    return it->second;
  }
  void* handle = uploader_ ? uploader_->upload(img.rgba.data(), img.width, img.height) : nullptr;
  if (uploader_ && !handle) return nullptr;
  std::shared_ptr<Texture> tex(new Texture());
  tex->width_ = tex->pixelWidth_ = img.width;
  tex->height_ = tex->pixelHeight_ = img.height;
  tex->path_ = key;
  tex->handle_ = handle;
  tex->uploader_ = uploader_;
  entries_[key] = tex;
  memory_[key] = img;
  alphas_.erase(key + "|");
  return tex;
}

std::shared_ptr<HitMask> TextureCache::loadMask(std::string_view name) const {
  Resolved r = resolve(name);
  if (r.path.empty()) {
    diag::err("[gfx] mask '%.*s' not found\n", static_cast<int>(name.size()), name.data());
    return nullptr;
  }
  auto bytes = fs_.readFile(r.path);
  ImageData img;
  if (!bytes || !decodeImage(*bytes, img)) {
    diag::err("[gfx] cannot decode mask '%s'\n", r.path.c_str());
    return nullptr;
  }
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3);
  for (std::size_t i = 0, n = rgb.size() / 3; i < n; ++i) {
    rgb[i * 3] = img.rgba[i * 4];
    rgb[i * 3 + 1] = img.rgba[i * 4 + 1];
    rgb[i * 3 + 2] = img.rgba[i * 4 + 2];
  }
  return std::make_shared<HitMask>(img.width, img.height, std::move(rgb));
}

const AlphaMask* TextureCache::alphaOf(const Texture& tex) const {
  const std::string key = tex.path_ + '|' + tex.maskPath_;
  if (auto it = alphas_.find(key); it != alphas_.end()) return it->second.get();
  ImageData img;
  bool ok = false;
  if (auto mem = memory_.find(tex.path_); mem != memory_.end()) {
    img = mem->second;
    ok = true;
  } else if (!tex.path_.empty()) {
    ok = loadPixels(tex.path_, tex.maskPath_, img);
  }
  std::unique_ptr<AlphaMask> mask;
  if (ok && img.width > 0 && img.height > 0) {
    std::vector<std::uint8_t> a(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = img.rgba[i * 4 + 3];
    mask = std::make_unique<AlphaMask>(img.width, img.height, std::move(a));
  }
  return (alphas_[key] = std::move(mask)).get();
}

void TextureCache::trim() {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.use_count() == 1) {
      memory_.erase(it->first);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void TextureCache::reuploadAll() {
  if (!uploader_) return;
  for (auto& [key, tex] : entries_) {
    ImageData img;
    if (auto mem = memory_.find(key); mem != memory_.end()) img = mem->second;
    else if (!loadPixels(tex->path_, tex->maskPath_, img)) {
      diag::err("[gfx] cannot reload texture '%s'\n", tex->path_.c_str());
      continue;
    }
    void* fresh = uploader_->upload(img.rgba.data(), img.width, img.height);
    if (!fresh) {
      diag::err("[gfx] cannot re-upload texture '%s'\n", tex->path_.c_str());
      continue;  // the stale handle stays; drawing with it is the renderer's problem
    }
    if (tex->handle_) uploader_->release(tex->handle_);
    tex->handle_ = fresh;
  }
}

}  // namespace choco::gfx
