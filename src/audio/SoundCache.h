#pragma once

#include <cstdint>
#include <functional>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "audio/OggStream.h"
#include "audio/Pcm.h"

namespace choco::audio {

// Decoded sounds and raw ogg bytes by asset path. Nothing is ever evicted (all non-music audio decodes to under about
// 43 MB for the 120 sounds the table reaches, docs/design/audio.md decision 3), so pointers handed out stay valid for the cache's lifetime. A path that
// cannot be read or decoded is remembered and logged once. Main thread only, except for the optional prefetch worker
// (startPrefetch), which only reads files and decodes; it never logs and never touches the maps below.
class SoundCache {
 public:
  using Bytes = std::vector<uint8_t>;
  using Loader = std::function<std::optional<Bytes>(const std::string& path)>;
  using LogSink = std::function<void(const std::string& line)>;

  SoundCache(Loader loader, LogSink log) : loader_(std::move(loader)), log_(std::move(log)) {}
  ~SoundCache();
  SoundCache(const SoundCache&) = delete;
  SoundCache& operator=(const SoundCache&) = delete;

  // Decodes every listed sound on one background thread, in order (docs/design/audio.md decision 3: sounds are decoded off the
  // main thread and the audio thread; it never sees a decode). `primary`/`fallback` follow pcm()'s rule. pcm() then finds
  // the finished decode, waits for one that is running, or takes one that has not started and decodes it itself, so a
  // sound that decodes is never decoded twice. A file that is missing or undecodable is left to pcm(), which logs it as usual.
  // `onStart` runs first on the worker (the app lowers the thread's priority there). Call once, before the first pcm().
  struct Request {
    std::string primary, fallback;
  };
  void startPrefetch(std::vector<Request> list, std::function<void()> onStart = {});
  // Main thread, each frame: once the worker has finished, logs "[audio] prefetched N sounds, M KB, in T ms" (once).
  void pollPrefetch();

  // Decoded on first use and kept; nullptr when missing or undecodable. With a non-empty `fallback`, a missing `path`
  // is not an error: the fallback is used and nothing is logged for `path` (the AU s.10.9 resource pairs).
  const Pcm* pcm(const std::string& path, const std::string& fallback = {});
  // Decoded afresh and NOT kept, for music: the caller owns it and frees it when the track is reaped, so a 95 s cue isn't
  // held for the rest of the run. (The ogg bytes are still cached. Music streams instead of decoding whole stems.)
  std::unique_ptr<Pcm> decodeUncached(const std::string& path, const std::string& fallback = {});
  // A stream over the file's cached bytes for music (the caller owns it; the cache outlives it). Null when missing or
  // undecodable (logged once). The fallback rule is the same as pcm()'s.
  std::unique_ptr<OggStream> openStream(const std::string& path, const std::string& fallback = {});
  // The file's bytes, read once; nullptr when missing (logged once unless `quiet`).
  const Bytes* bytes(const std::string& path, bool quiet = false);

  std::size_t decodedCount() const { return pcm_.size(); }

 private:
  void logOnce(const std::string& path, const std::string& line);
  std::unique_ptr<Pcm> decodeBytes(const Bytes& raw, DecodeError* error) const;
  void prefetchLoop(std::function<void()> onStart);

  Loader loader_;
  LogSink log_;
  std::map<std::string, std::unique_ptr<Pcm>> pcm_;  // null entry = failed
  std::map<std::string, std::unique_ptr<Bytes>> bytes_;
  std::set<std::string> logged_;

  // Prefetch state, all under mutex_. A job is Queued until the worker or pcm() takes it (Running, or Taken by the main
  // thread), then Done (result holds the Pcm, or null when it failed) once the worker has finished it.
  enum class Job { Queued, Running, Taken, Done };
  struct Entry {
    Request req;
    Job state = Job::Queued;
    std::unique_ptr<Pcm> result;
  };
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::string> order_;  // keys of entries_, in request order
  std::map<std::string, Entry> entries_;  // keyed by the primary path
  bool stop_ = false;
  bool finished_ = false;  // the worker has run through its list
  bool reported_ = false;
  std::size_t prefetchedCount_ = 0, prefetchedBytes_ = 0;
  double prefetchMs_ = 0;
  std::thread worker_;
};

}  // namespace choco::audio
