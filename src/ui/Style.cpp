#include "ui/Style.h"

#include <cstdio>

namespace choco::ui {

std::optional<double> MapStyle::number(const char* key) const {
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  if (auto* d = std::get_if<double>(&it->second)) return *d;
  return std::nullopt;
}

std::optional<std::string> MapStyle::string(const char* key) const {
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  if (auto* s = std::get_if<std::string>(&it->second)) return *s;
  return std::nullopt;
}

std::optional<bool> MapStyle::boolean(const char* key) const {
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  if (auto* d = std::get_if<double>(&it->second)) return *d != 0;
  return false;
}

std::optional<std::vector<double>> MapStyle::numbers(const char* key, int count, double def) const {
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  auto* v = std::get_if<std::vector<double>>(&it->second);
  if (!v) return std::nullopt;
  std::vector<double> out(static_cast<std::size_t>(count), def);
  for (std::size_t i = 0; i < out.size() && i < v->size(); ++i) out[i] = (*v)[i];
  return out;
}

std::optional<std::vector<std::string>> MapStyle::strings(const char* key, int count) const {
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  auto* v = std::get_if<std::vector<std::string>>(&it->second);
  if (!v) return std::nullopt;
  std::vector<std::string> out(static_cast<std::size_t>(count));
  for (std::size_t i = 0; i < out.size() && i < v->size(); ++i) out[i] = (*v)[i];
  return out;
}

void MapStyle::forEachEntry(const EntryFn& fn) const {
  for (const auto& [k, v] : values_) {
    if (auto* t = std::get_if<std::shared_ptr<MapStyle>>(&v)) {
      if (*t) fn(k, t->get(), std::nullopt);
    } else if (auto* d = std::get_if<double>(&v)) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "%g", *d);
      fn(k, nullptr, std::string(buf));
    } else if (auto* str = std::get_if<std::string>(&v)) {
      fn(k, nullptr, *str);
    } else {
      fn(k, nullptr, std::nullopt);
    }
  }
}

}  // namespace choco::ui

namespace choco::ui {

std::optional<std::string> MapStyle::tableString(const char* key, int index) const {
  return string((std::string(key) + "." + std::to_string(index)).c_str());
}

std::optional<std::vector<double>> MapStyle::tableNumbers(const char* key, int index, int count, double def) const {
  return numbers((std::string(key) + "." + std::to_string(index)).c_str(), count, def);
}

}  // namespace choco::ui

namespace choco::ui {

std::unique_ptr<const Style> MapStyle::table(const char* key) const {
  auto it = values_.find(key);
  if (it == values_.end()) return nullptr;
  auto* t = std::get_if<std::shared_ptr<MapStyle>>(&it->second);
  if (!t || !*t) return nullptr;
  return std::make_unique<MapStyle>(**t);
}

std::unique_ptr<const Style> MapStyle::element(int i) const {
  if (i < 1 || i > size()) return nullptr;
  auto* t = std::get_if<std::shared_ptr<MapStyle>>(&array_[static_cast<std::size_t>(i - 1)]);
  if (!t || !*t) return nullptr;
  return std::make_unique<MapStyle>(**t);
}

std::optional<double> MapStyle::numberAt(int i) const {
  if (i < 1 || i > size()) return std::nullopt;
  if (auto* d = std::get_if<double>(&array_[static_cast<std::size_t>(i - 1)])) return *d;
  return std::nullopt;
}

std::optional<std::string> MapStyle::stringAt(int i) const {
  if (i < 1 || i > size()) return std::nullopt;
  if (auto* s = std::get_if<std::string>(&array_[static_cast<std::size_t>(i - 1)])) return *s;
  return std::nullopt;
}

void MapStyle::forEachTable(const std::function<void(const Style&)>& fn) const {
  for (const Element& e : array_)
    if (auto* t = std::get_if<std::shared_ptr<MapStyle>>(&e); t && *t) fn(**t);
  for (const auto& [k, v] : values_)
    if (auto* t = std::get_if<std::shared_ptr<MapStyle>>(&v); t && *t) fn(**t);
}

void MapStyle::forEachNumber(const std::function<void(const std::string&, double)>& fn) const {
  for (const auto& [k, v] : values_)
    if (auto* d = std::get_if<double>(&v)) fn(k, *d);
}

}  // namespace choco::ui
