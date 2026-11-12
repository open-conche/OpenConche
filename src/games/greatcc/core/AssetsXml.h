#pragma once

// assets.xml (docs/re/greatcc-audio.md 6): the sounds the game decodes ahead at level load.

#include <string>
#include <string_view>
#include <vector>

namespace choco::games::greatcc {

struct AssetSound {
  std::string file;
  int group = 0;  // the `type` attribute: the sound group id (to_int of a missing one is 0)
};

// The <sound file= type= memory=> entries of an <assets> document, in file order. Texture entries and anything else are not
// listed; a document that does not parse gives an empty list.
std::vector<AssetSound> parseAssetSounds(std::string_view xml);

}  // namespace choco::games::greatcc
