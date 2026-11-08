#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "assets/AssetFS.h"
#include "assets/Xml.h"

namespace choco::assets {

// One data element: child element name -> collapsed text, in file order. Every value is a string, as in the original.
using KeyValues = std::vector<std::pair<std::string, std::string>>;

// Problems found while loading (unparseable files, wrong root); loading continues past them.
using Warnings = std::vector<std::string>;

// Parses `path` and checks the root element name (any root when rootName is null). Returns false, adding a warning, on
// failure.
bool openXml(const AssetFS& fs, const std::string& path, const char* rootName, Xml& xml, Warnings* w);

// Exactly "::assets/settings.xml" (cwd-relative, else the assets root): each child of the root becomes a key with its text.
KeyValues loadSettings(const AssetFS& fs, Warnings* warnings = nullptr);

// Excel-2003 spreadsheet XML rows (Workbook/Worksheet/Table/Row/Cell/Data) of the first worksheet: first cell = key,
// second = value (missing = empty). Rows with an empty key are skipped.
std::vector<std::pair<std::string, std::string>> readExcelRows(const Xml& xml);

// The game has two string layers (docs/re/startup-and-assets.md, "Strings"):
//  - the BSG map (bsgLoadStringFile / bsgGetString), which Reforged's Lua uses, with no arguments;
//  - the SDK table (strings.xml, TStringTable) with %1%..%5% arguments, used by LookupString and as the BSG fallback.
class StringTable {
 public:
  enum class Layer { Bsg, Sdk, Both };

  // Merges an Excel-XML string file into a layer; later loads overwrite earlier keys. A missing file is ignored
  // (returns false).
  bool loadFile(const AssetFS& fs, std::string_view path, Layer layer = Layer::Both, Warnings* warnings = nullptr);
  void loadRows(const std::vector<std::pair<std::string, std::string>>& rows, Layer layer = Layer::Both);
  bool has(const std::string& key) const { return bsg_.count(key) != 0 || sdk_.count(key) != 0; }
  // BSGStringTable::StringExists: the BSG map only, no SDK fallback.
  bool hasBsg(const std::string& key) const { return bsg_.count(key) != 0; }
  std::size_t size() const { return bsg_.size(); }

  // BSGStringTable::GetString. A leading '#' returns the rest literally. The text comes from the BSG map, else from
  // sdkLookup(key) with no arguments. Then every pair of '%' signs is processed left to right: "%%" becomes one '%'
  // (the kept '%' is not rescanned); anything between two '%' is a key, looked up recursively through this same
  // function, including "1". A nested "#####" becomes the marker "-->key<--". The replacement is not rescanned and,
  // as in the original, the character right after it is skipped. A '%' with no closing '%' stays. Missing key: "#####".
  std::string lookup(const std::string& key) const;

  // TStringTable::GetString with arguments (at most five are used; missing ones are empty). %N% inserts argument N,
  // %name% inserts the raw value of another entry, "%%" a literal '%'. An argument of exactly "%%" inserts '%' and
  // one written %key% inserts that entry's raw value. The whole result is "#####" when an entry is missing, N is
  // not 1..5, or an unclosed '%' is followed by text. A leading '#' returns the rest literally.
  std::string sdkLookup(const std::string& key, const std::vector<std::string>& args = {}) const;

 private:
  std::string bsgGet(const std::string& key, int depth) const;
  std::unordered_map<std::string, std::string> bsg_, sdk_;
};

}  // namespace choco::assets
