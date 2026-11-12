#pragma once

// VLuaStyle's layered lookup (docs/re/greatcc-boot-and-natives.md 4.1) over ui::Style views: a key is searched in the table,
// then in the table stored under its `style` key (that one only), then in the parent chain with the depth reduced by one.
// A depth of -1 searches every ancestor, 0 none. Used by the sprite loaders, whose tables nest (an object's assets and
// children read the keys they lack from the object around them).
//
// Not owning: `own` and `parent` must outlive the chain. A chain made from a LuaStyle view must be destroyed before any view
// made earlier (the views share a stack); building them recursively inside the iteration callbacks does that.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ui/Style.h"

namespace choco::games::greatcc {

class StyleChain {
 public:
  StyleChain(const ui::Style& own, const StyleChain* parent) : own_(own), base_(own.table("style")), parent_(parent) {}
  StyleChain(const StyleChain&) = delete;
  StyleChain& operator=(const StyleChain&) = delete;

  const ui::Style& own() const { return own_; }
  const StyleChain* parent() const { return parent_; }

  std::optional<double> number(const char* key, int depth = -1) const {
    return find<double>(depth, [&](const ui::Style& s) { return s.number(key); });
  }
  std::optional<std::string> string(const char* key, int depth = -1) const {
    return find<std::string>(depth, [&](const ui::Style& s) { return s.string(key); });
  }
  std::optional<bool> boolean(const char* key, int depth = -1) const {
    // A table under the key is not a boolean (an asset named `anim` sits in the same group as the `anim = false` flag): the
    // search goes on past it.
    return find<bool>(depth, [&](const ui::Style& s) -> std::optional<bool> {
      if (s.table(key)) return std::nullopt;
      return s.boolean(key);
    });
  }
  std::optional<std::vector<double>> numbers(const char* key, int count, double def, int depth = -1) const {
    return find<std::vector<double>>(depth, [&](const ui::Style& s) { return s.numbers(key, count, def); });
  }
  // The table at `key`, searched like any other key. The view is valid while the chain that found it is.
  std::unique_ptr<const ui::Style> table(const char* key, int depth = -1) const {
    if (auto t = own_.table(key)) return t;
    if (base_)
      if (auto t = base_->table(key)) return t;
    if (parent_ && depth != 0) return parent_->table(key, depth < 0 ? -1 : depth - 1);
    return nullptr;
  }

  double numberOr(const char* key, double def, int depth = -1) const { return number(key, depth).value_or(def); }
  int intOr(const char* key, int def, int depth = -1) const {
    return static_cast<int>(number(key, depth).value_or(static_cast<double>(def)));
  }
  bool boolOr(const char* key, bool def, int depth = -1) const { return boolean(key, depth).value_or(def); }
  std::string stringOr(const char* key, const std::string& def, int depth = -1) const {
    return string(key, depth).value_or(def);
  }

 private:
  template <class T, class Get>
  std::optional<T> find(int depth, const Get& get) const {
    if (auto v = get(own_)) return v;
    if (base_)
      if (auto v = get(*base_)) return v;
    if (parent_ && depth != 0) return parent_->find<T>(depth < 0 ? -1 : depth - 1, get);
    return std::nullopt;
  }

  const ui::Style& own_;
  std::unique_ptr<const ui::Style> base_;
  const StyleChain* parent_;
};

}  // namespace choco::games::greatcc
