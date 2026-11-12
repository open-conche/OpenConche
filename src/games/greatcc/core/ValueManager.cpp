#include "games/greatcc/core/ValueManager.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>

#include "assets/Xml.h"

namespace choco::games::greatcc {

namespace {

constexpr int kMaxByvalDepth = 8;

double toNumber(const char* s) {
  if (!s || !*s) return 0;
  return std::strtod(s, nullptr);
}

}  // namespace

bool ValueManager::load(std::string_view xml) {
  schema_ = std::string(xml);
  reload();
  return !values_.empty() || schema_.empty();
}

void ValueManager::reload() {
  values_.clear();
  assets::Xml doc;
  if (schema_.empty() || !doc.parse(schema_)) return;
  for (pugi::xml_node n : doc.root().children("value")) {
    const std::string name = n.attribute("name").as_string();
    if (name.empty()) continue;
    Value v;
    v.base = v.initial = toNumber(n.attribute("value").as_string());
    v.min = -DBL_MAX;
    v.max = DBL_MAX;
    if (*n.attribute("min").as_string()) v.min = toNumber(n.attribute("min").as_string());
    if (*n.attribute("max").as_string()) v.max = toNumber(n.attribute("max").as_string());
    v.saved = std::string(n.attribute("saved").as_string()) == "true";
    for (pugi::xml_node m : n.children("modify")) {
      Modifier mod;
      mod.amount = toNumber(m.attribute("value").as_string());
      mod.category = m.attribute("category").as_string();
      if (*m.attribute("upgrade").as_string()) {
        mod.type = 0;
        mod.name = m.attribute("upgrade").as_string();
      } else if (*m.attribute("plot").as_string()) {
        mod.type = 1;
        mod.name = m.attribute("plot").as_string();
      } else if (*m.attribute("byval").as_string()) {
        mod.type = 2;
        mod.name = m.attribute("byval").as_string();
      }
      v.modifiers.push_back(std::move(mod));
    }
    if (v.saved) v.base = progress_.getDouble("value." + name, v.initial);
    values_[name] = std::move(v);
  }
}

ValueManager::Value& ValueManager::ensure(const std::string& name, double def) {
  auto it = values_.find(name);
  if (it == values_.end()) {
    Value v;
    v.base = v.initial = def;
    v.min = -DBL_MAX;
    v.max = DBL_MAX;
    it = values_.emplace(name, std::move(v)).first;
  }
  return it->second;
}

double ValueManager::evaluate(const std::string& name) {
  Value& v = ensure(name, 0);
  const double base = v.base;
  double r = base;
  for (auto it = v.modifiers.rbegin(); it != v.modifiers.rend(); ++it) {
    double amount = it->amount;
    bool active = false;
    switch (it->type) {
      case 0: active = upgradeActive_ && upgradeActive_(it->name, it->category); break;
      case 1: active = plotActive_ && plotActive_(it->name); break;
      case 2:
        active = true;
        amount = depth_ < kMaxByvalDepth ? getReal(it->name, 0) : 0;  // the amount is another value, read now
        break;
      default: active = true; break;
    }
    if (active) r += base * amount;
  }
  const Value& cur = values_.find(name)->second;  // a byval read may have grown the table, never moved an element
  r = std::min(r, cur.max);
  r = std::max(r, cur.min);
  return r;
}

double ValueManager::getReal(const std::string& name, double def) {
  ensure(name, def);
  ++depth_;
  const double r = evaluate(name);
  --depth_;
  return r;
}

int ValueManager::getInt(const std::string& name, int def) {
  const double v = std::floor(static_cast<float>(getReal(name, def)));
  if (v >= 2147483647.0) return 2147483647;
  if (v <= -2147483648.0) return -2147483647 - 1;
  return static_cast<int>(v);
}

float ValueManager::getSum(const std::string& name, float delta, bool clamp) {
  float v = static_cast<float>(getReal(name, 0)) + delta;
  if (clamp) {
    const float lo = static_cast<float>(minOf(name)), hi = static_cast<float>(maxOf(name));
    const float capped = hi < v ? hi : v;
    if (capped <= lo) v = lo;
    else if (hi < v) v = hi;
  }
  return v;
}

float ValueManager::getProduct(const std::string& name, float x, bool clamp) {
  float v = static_cast<float>(getReal(name, 1.0)) * x;
  if (clamp) {
    const float lo = static_cast<float>(minOf(name)), hi = static_cast<float>(maxOf(name));
    const float held = hi < v ? hi : v;
    v = held <= lo ? lo : (hi < v ? hi : v);
  }
  return v;
}

std::uint32_t ValueManager::getUint(const std::string& name, std::uint32_t def) {
  const double v = std::floor(static_cast<float>(getReal(name, def)));
  if (v <= 0) return 0;
  if (v >= 4294967295.0) return 4294967295u;
  return static_cast<std::uint32_t>(v);
}

void ValueManager::store(const std::string& name, const Value& v) {
  if (v.saved && progress_.hasUser()) progress_.setDouble("value." + name, v.base);
}

void ValueManager::setValue(const std::string& name, double v, bool save) {
  auto it = values_.find(name);
  if (it == values_.end()) {
    Value& created = ensure(name, v);
    if (save) store(name, created);
    return;
  }
  Value& val = it->second;
  val.base = std::max(std::min(v, val.max), val.min);
  if (save) store(name, val);
}

void ValueManager::addToValue(const std::string& name, double delta, double def, bool save) {
  Value& v = ensure(name, def);
  v.base += delta;
  if (save) store(name, v);
}

double ValueManager::minOf(const std::string& name) const {
  auto it = values_.find(name);
  return it == values_.end() ? static_cast<double>(FLT_MIN) : it->second.min;
}

double ValueManager::maxOf(const std::string& name) const {
  auto it = values_.find(name);
  return it == values_.end() ? static_cast<double>(FLT_MAX) : it->second.max;
}

void ValueManager::saveAll() {
  for (const auto& [name, v] : values_) store(name, v);
}

}  // namespace choco::games::greatcc
