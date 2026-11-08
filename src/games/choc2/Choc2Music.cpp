#include "games/choc2/Choc2Music.h"

#include <vector>

#include "audio/Audio.h"
#include "audio/Mixer.h"

namespace choco::game::choc2 {

namespace {

constexpr int kMusicFadeOutMs = 1000;  // Chocolatier 1's rule, assumed for Chocolatier 2
const char* const kMenuTrack = "music/gameplay4.ogg";

}  // namespace

void Choc2Music::startRotation(audio::Audio& a) {
  // The next of gameplay1..3 other than the last one played, from the audio RNG stream.
  std::vector<int> open;
  for (int i = 0; i < 3; ++i) {
    if (i != last_) open.push_back(i);
  }
  const int pick = open[static_cast<std::size_t>(a.uniform() * static_cast<double>(open.size()))];
  last_ = pick;
  file_.clear();
  mode_ = a.playMusicFile("music/gameplay" + std::to_string(pick + 1) + ".ogg", false, 0, kMusicFadeOutMs) ? Mode::Rotation : Mode::None;
}

void Choc2Music::tick(audio::Audio& a) {
  // A one-shot ended, or a rotation track did: the normal state goes on with the next gameplay track (nothing in the
  // game re-queues, so the native does).
  if (mode_ == Mode::Sting && !a.musicPlaying() && silentAfter_) {
    mode_ = Mode::Silent;
    silentAfter_ = false;
  } else if ((mode_ == Mode::Pending || mode_ == Mode::Sting || mode_ == Mode::Rotation) && !a.musicPlaying()) {
    startRotation(a);
  } else if (mode_ == Mode::Pending) {
    startRotation(a);  // the track it replaces fades out over 1 s
  }
}

void Choc2Music::setState(audio::Audio& a, const std::string& state, const std::string& file) {
  a.mixer().setMusicPaused(false);  // any state request resumes a paused track (RestartGame never calls ResumeMusic)
  auto ignored = [&] { a.log("[audio] music state '" + state + "' " + file + " ignored (already set)"); };
  auto loopState = [&](Mode mode, const char* track) {
    if (mode_ == mode && a.musicPlaying()) return ignored();
    file_ = track;
    mode_ = a.playMusicFile(track, true, 0, kMusicFadeOutMs) ? mode : Mode::None;
  };
  if (state == "normal" && !file.empty()) {
    const bool menu = file == kMenuTrack;
    const Mode mode = menu ? Mode::Menu : Mode::Sting;
    if (mode_ == mode && file_ == file && a.musicPlaying()) return ignored();
    file_ = file;
    // A sting whose file is missing still hands back to the rotation (tick).
    mode_ = a.playMusicFile(file, menu, 0, kMusicFadeOutMs) || !menu ? mode : Mode::None;
  } else if (state == "normal") {
    // The rotation starts on the next update(), so a "silent" in the same frame (a hidden port's LPort:SetAmbient follows
    // the view's "normal") cancels it before any track starts.
    if ((mode_ == Mode::Rotation || mode_ == Mode::Sting) && a.musicPlaying()) {
      silentAfter_ = false;
      return ignored();
    }
    if (mode_ == Mode::Pending) return ignored();
    mode_ = Mode::Pending;
    silentAfter_ = false;
  } else if (state == "silent") {
    if (mode_ == Mode::Silent) return ignored();
    if (mode_ == Mode::Sting && a.musicPlaying()) {  // a sting is not cut: quiet once it has ended
      silentAfter_ = true;
      return ignored();
    }
    silentAfter_ = false;
    a.stopMusicTrack(kMusicFadeOutMs);
    mode_ = Mode::Silent;
    file_.clear();
    a.log("[audio] music state silent");
  } else if (state == "factory_wait") {
    loopState(Mode::Wait, "music/minigame_wait.ogg");
  } else if (state.empty()) {
    loopState(Mode::Mini, "music/minigame.ogg");
  } else if (unknownLogged_.insert("state:" + state).second) {
    a.log("[audio] unknown music state '" + state + "'");
  }
}

}  // namespace choco::game::choc2
