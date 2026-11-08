#include "games/choc3/ContentLoaders.h"

namespace choco::assets {

namespace {

void warn(Warnings* w, std::string msg) {
  if (w) w->push_back(std::move(msg));
}

std::vector<KeyValues> loadItems(const AssetFS& fs, const char* suffix, const char* element, Warnings* w) {
  std::vector<KeyValues> out;
  for (const std::string& path : fs.listBySuffix(suffix)) {
    Xml xml;
    if (!openXml(fs, path, "root", xml, w)) continue;
    for (auto n : xml.root().children(element)) {
      KeyValues kv;
      // Nested elements become nested Lua tables in the original; no shipped data has any, so they are flattened to text.
      for (auto c : n.children()) kv.emplace_back(c.name(), nodeText(c));
      out.push_back(std::move(kv));
    }
  }
  return out;
}

// The original keeps the path up to the first '/' at index 4 or later; with none it uses the whole path unchanged.
std::string listDir(const std::string& path) {
  auto p = path.find('/', 4);
  return p == std::string::npos ? path : path.substr(0, p);
}

std::vector<std::string> loadFileList(const AssetFS& fs, const char* suffix, const char* element, Warnings* w) {
  std::vector<std::string> out;
  for (const std::string& listPath : fs.listBySuffix(suffix)) {
    Xml xml;
    if (!openXml(fs, listPath, "root", xml, w)) continue;
    const std::string prefix = listDir(listPath) + "/";
    for (auto n : xml.root().children(element)) {
      bool hasName = false;
      std::string name = childText(n, "name", &hasName);
      if (!hasName) name = nodeText(n);
      if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0) name.resize(name.size() - 4);
      if (name.empty()) continue;
      // The doc shows two existence checks, the second a fallback; the order is an inference confirmed by the Reforged
      // layout (ports/capetown/capetown.lua, quests/tutorial.lua).
      if (auto r = fs.resolve(prefix + name + "/" + name + ".lua")) out.push_back(*r);
      else if (auto r2 = fs.resolve(prefix + name + ".lua")) out.push_back(*r2);
      else warn(w, listPath + ": no script found for '" + name + "'");
    }
  }
  return out;
}

}  // namespace

std::vector<KeyValues> loadIngredients(const AssetFS& fs, Warnings* w) {
  return loadItems(fs, "ingredients.xml", "ingredient", w);
}
std::vector<KeyValues> loadProducts(const AssetFS& fs, Warnings* w) { return loadItems(fs, "products.xml", "product", w); }
std::vector<std::string> loadPortFileList(const AssetFS& fs, Warnings* w) { return loadFileList(fs, "ports.xml", "port", w); }
std::vector<std::string> loadQuestFileList(const AssetFS& fs, Warnings* w) { return loadFileList(fs, "quests.xml", "file", w); }

}  // namespace choco::assets
