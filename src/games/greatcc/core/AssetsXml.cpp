#include "games/greatcc/core/AssetsXml.h"

#include <cstdlib>

#include "assets/Xml.h"

namespace choco::games::greatcc {

std::vector<AssetSound> parseAssetSounds(std::string_view xml) {
  std::vector<AssetSound> out;
  assets::Xml doc;
  if (!doc.parse(xml)) return out;
  for (pugi::xml_node n : doc.root().children("sound"))
    out.push_back({n.attribute("file").value(), std::atoi(n.attribute("type").value())});
  return out;
}

}  // namespace choco::games::greatcc
