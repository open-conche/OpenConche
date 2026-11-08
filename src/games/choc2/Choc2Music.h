#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace choco::audio {
class Audio;
}

namespace choco::game::choc2 {

// Chocolatier 2's SetMusicState(state[, file]) state machine (docs/re/choc2-audio.md), on top of the Audio service's
// generic file music. Owned by the Choc2World; the Audio service is passed in on every call, so nothing dangles.
class Choc2Music {
 public:
  void setState(audio::Audio& a, const std::string& state, const std::string& file = {});
  // Once per frame right after Audio::update (GameState::afterAudio): hands a finished one-shot or rotation track on.
  void tick(audio::Audio& a);

 private:
  enum class Mode : uint8_t { None, Pending, Rotation, Sting, Menu, Silent, Wait, Mini };
  void startRotation(audio::Audio& a);

  Mode mode_ = Mode::None;
  std::string file_;           // the file of the current Sting / Menu request
  bool silentAfter_ = false;   // a "silent" arrived during a sting: go quiet when it ends
  int last_ = -1;              // the gameplay file the rotation played last (0..2)
  std::set<std::string> unknownLogged_;
};

}  // namespace choco::game::choc2
