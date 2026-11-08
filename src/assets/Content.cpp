#include "assets/Content.h"

#include <algorithm>
#include <cstdlib>

#include "assets/Xml.h"

namespace choco::assets {

namespace {

void warn(Warnings* w, std::string msg) {
  if (w) w->push_back(std::move(msg));
}

}  // namespace

// Parses `path`; checks the root element name. Returns false (and warns) on failure.
bool openXml(const AssetFS& fs, const std::string& path, const char* rootName, Xml& xml, Warnings* w) {
  auto data = fs.readFile(path);
  if (!data) {
    warn(w, path + ": cannot read");
    return false;
  }
  if (!xml.parse(*data)) {
    warn(w, path + ": " + xml.error());
    return false;
  }
  if (rootName && std::string(xml.root().name()) != rootName) {
    warn(w, path + ": root element is <" + xml.root().name() + ">, expected <" + rootName + ">");
    return false;
  }
  return true;
}

KeyValues loadSettings(const AssetFS& fs, Warnings* w) {
  KeyValues out;
  const std::string path = "::assets/settings.xml";
  if (!fs.exists(path)) {
    warn(w, path + ": not found");
    return out;
  }
  Xml xml;
  // The root is <settings> in the Reforged data (not <root>), so any root name is accepted.
  if (!openXml(fs, path, nullptr, xml, w)) return out;
  for (auto c : xml.root().children()) out.emplace_back(c.name(), nodeText(c));
  return out;
}

std::vector<std::pair<std::string, std::string>> readExcelRows(const Xml& xml) {
  std::vector<std::pair<std::string, std::string>> rows;
  auto table = xml.root().child("Worksheet").child("Table");
  for (auto row : table.children("Row")) {
    std::string cells[2];
    int i = 0;
    for (auto cell : row.children("Cell")) {
      if (i >= 2) break;
      cells[i++] = nodeText(cell.child("Data"));
    }
    if (cells[0].empty()) continue;
    rows.emplace_back(std::move(cells[0]), std::move(cells[1]));
  }
  return rows;
}

bool StringTable::loadFile(const AssetFS& fs, std::string_view path, Layer layer, Warnings* w) {
  auto data = fs.readFile(path);
  if (!data) return false;
  Xml xml;
  if (!xml.parse(*data)) {
    warn(w, std::string(path) + ": " + xml.error());
    return false;
  }
  loadRows(readExcelRows(xml), layer);
  return true;
}

void StringTable::loadRows(const std::vector<std::pair<std::string, std::string>>& rows, Layer layer) {
  for (const auto& [k, v] : rows) {
    if (layer != Layer::Sdk) bsg_[k] = v;
    if (layer != Layer::Bsg) sdk_[k] = v;
  }
}

std::string StringTable::lookup(const std::string& key) const { return bsgGet(key, 0); }

std::string StringTable::bsgGet(const std::string& key, int depth) const {
  if (!key.empty() && key[0] == '#') return key.substr(1);
  if (depth > 16) return "#####";  // the original would recurse forever on a cycle
  std::string s;
  if (auto it = bsg_.find(key); it != bsg_.end()) s = it->second;
  else s = sdkLookup(key);

  std::size_t next = 0;
  for (;;) {
    const std::size_t a = s.find('%', next);
    if (a == std::string::npos) break;
    const std::size_t b = s.find('%', a + 1);
    if (b == std::string::npos) break;
    if (b == a + 1) {
      s.erase(a, 1);
      next = a + 1;
      continue;
    }
    const std::string name = s.substr(a + 1, b - a - 1);
    std::string rep = bsgGet(name, depth + 1);
    if (rep == "#####") rep = "-->" + name + "<--";
    s.replace(a, b - a + 1, rep);
    next = a + rep.size() + 1;  // sic: the original skips the character after the replacement
  }
  return s;
}

std::string StringTable::sdkLookup(const std::string& key, const std::vector<std::string>& args) const {
  if (!key.empty() && key[0] == '#') return key.substr(1);
  auto it = sdk_.find(key);
  if (it == sdk_.end()) return "#####";
  const std::string& text = it->second;
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') {
      out += text[i];
      continue;
    }
    const std::size_t close = text.find('%', i + 1);
    const std::string name = text.substr(i + 1, close == std::string::npos ? std::string::npos : close - i - 1);
    if (close == std::string::npos) {
      if (name.empty()) {  // a trailing lone '%' is kept
        out += '%';
        break;
      }
      return "#####";  // "Non-matched percent" with text after it
    }
    i = close;
    if (name.empty()) {  // "%%"
      out += '%';
      continue;
    }
    if (name.find_first_not_of("0123456789") == std::string::npos) {
      const long n = std::atol(name.c_str());
      if (n < 1 || n > 5) return "#####";
      const std::string arg = static_cast<std::size_t>(n) <= args.size() ? args[static_cast<std::size_t>(n - 1)] : std::string();
      if (!arg.empty() && arg[0] == '%') {
        if (arg == "%%") {
          out += '%';
        } else if (arg.size() >= 2 && arg.back() == '%') {
          auto r = sdk_.find(arg.substr(1, arg.size() - 2));
          if (r == sdk_.end()) return "#####";
          out += r->second;
        } else {
          return "#####";
        }
      } else {
        out += arg;
      }
    } else {
      auto r = sdk_.find(name);
      if (r == sdk_.end()) return "#####";
      out += r->second;
    }
  }
  return out;
}

}  // namespace choco::assets
