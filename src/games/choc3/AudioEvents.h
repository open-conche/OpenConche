#pragma once

#include <memory>
#include <string_view>

#include "assets/AssetFS.h"
#include "audio/EventTable.h"

namespace choco::games::choc3 {

// Chocolatier 3's SoundEvent table, built once from audio/generated/AudioEvents.inc.
const audio::EventTable& audioEvents();

// The table for the Macintosh build: the compiled cadilite.spec plus the native rows of the Windows table that the spec
// does not hold (docs/design/mac-audio.md s.2). A spec event with the same name as a native row wins. Load messages (parse
// errors, events with no engine equivalent) are appended to `messages` when it is not null.
audio::EventTable macAudioEvents(std::string_view specText, std::vector<std::string>* messages = nullptr);

// What GameModule::makeAudioEvents returns for Chocolatier 3: the Macintosh table when the assets hold cadi/cadilite.spec
// (its load messages go to the log), otherwise a copy of audioEvents().
std::unique_ptr<const audio::EventTable> makeAudioEvents(const assets::AssetFS& fs);

}  // namespace choco::games::choc3
