#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <pugixml.hpp>

#include "assets/AssetSource.h"

namespace choco::assets {

// Splits on whitespace and rejoins with single spaces, like the original's tokenizer.
std::string collapseWhitespace(std::string_view s);

// Text of an element: all descendant character data, entities decoded, whitespace collapsed.
std::string nodeText(const pugi::xml_node& n);

// First child element with this name, as collapsed text; empty if absent. `found` tells the two apart.
std::string childText(const pugi::xml_node& n, const char* name, bool* found = nullptr);

// An XML document parsed from memory.
class Xml {
 public:
  bool parse(const Bytes& data);
  bool parse(std::string_view text);
  bool ok() const { return error_.empty(); }
  const std::string& error() const { return error_; }
  pugi::xml_node root() const { return doc_.document_element(); }

 private:
  bool finish(const pugi::xml_parse_result& r);
  pugi::xml_document doc_;
  std::string error_;
};

}  // namespace choco::assets
