#include "games/choc1/Choc1Music.h"

#include "audio/Audio.h"

namespace choco::game::choc1 {

namespace {
constexpr const char* kMusicDir = "audio/music/";
}

bool AudioMusicSink::play(const std::string& path) { return a_.playMusicFile(path, false, 0, Choc1Music::kFadeMs); }
void AudioMusicSink::fadeOutAndStop(int ms) { a_.stopMusic(ms); }
bool AudioMusicSink::playing() const { return a_.musicPlaying(); }

bool Choc1Music::begin(MusicSink& sink, const std::string& file) {
  if (!sink.play(std::string(kMusicDir) + file)) {
    state_ = State::Idle;
    file_.clear();
    return false;
  }
  state_ = State::Playing;
  file_ = file;
  return true;
}

void Choc1Music::advance(MusicSink& sink, double nowMs) {
  state_ = State::Idle;
  file_.clear();
  while (!queue_.empty()) {
    const Entry e = queue_.front();
    queue_.pop_front();
    if (e.seconds > 0) {  // a wait of `seconds` of silence, then the track
      state_ = State::Waiting;
      file_ = e.file;
      untilMs_ = nowMs + e.seconds * 1000.0;
      return;
    }
    if (begin(sink, e.file)) return;  // a missing file is skipped
  }
}

void Choc1Music::start(MusicSink& sink, const std::string& file, double nowMs) {
  if (state_ == State::Playing && file == file_) return;
  queue_.clear();
  if (file.empty()) {
    if (state_ == State::Playing) {
      sink.fadeOutAndStop(kFadeMs);
      state_ = State::Fading;
      untilMs_ = nowMs + kFadeMs;
    } else if (state_ == State::Waiting) {
      state_ = State::Idle;
    }
    file_.clear();
    return;
  }
  if (state_ == State::Idle || state_ == State::Waiting) {
    begin(sink, file);
    return;
  }
  if (state_ == State::Playing) {
    sink.fadeOutAndStop(kFadeMs);
    state_ = State::Fading;
    file_.clear();
    untilMs_ = nowMs + kFadeMs;
  }
  queue_.push_back({file, 0});  // starts when the fade has finished (a fade in progress keeps its end)
}

void Choc1Music::queue(MusicSink& sink, const std::string& file, double seconds, double nowMs) {
  (void)nowMs;
  if (file.empty()) return;
  if (state_ == State::Idle) {
    begin(sink, file);
    return;
  }
  queue_.push_back({file, seconds});
}

void Choc1Music::tick(MusicSink& sink, double nowMs) {
  switch (state_) {
    case State::Playing:
      if (!sink.playing()) advance(sink, nowMs);  // it ended on its own
      break;
    case State::Fading:
      if (nowMs >= untilMs_) advance(sink, nowMs);
      break;
    case State::Waiting:
      if (nowMs >= untilMs_) {
        const std::string file = file_;
        if (!begin(sink, file)) advance(sink, nowMs);
      }
      break;
    case State::Idle:
      break;
  }
}

}  // namespace choco::game::choc1
