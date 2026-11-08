#include "audio/SoundCache.h"

#include <chrono>
#include <cstdio>

namespace choco::audio {

SoundCache::~SoundCache() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SoundCache::logOnce(const std::string& path, const std::string& line) {
  if (logged_.insert(path).second && log_) log_(line);
}

const SoundCache::Bytes* SoundCache::bytes(const std::string& path, bool quiet) {
  auto it = bytes_.find(path);
  if (it != bytes_.end()) return it->second.get();
  std::unique_ptr<Bytes> data;
  if (loader_) {
    if (auto b = loader_(path)) data = std::make_unique<Bytes>(std::move(*b));
  }
  if (!data && !quiet) logOnce(path, "[audio] missing file " + path);
  return (bytes_[path] = std::move(data)).get();
}

std::unique_ptr<Pcm> SoundCache::decodeBytes(const Bytes& raw, DecodeError* error) const {
  if (auto decoded = decodeOgg(raw, error)) return std::make_unique<Pcm>(std::move(*decoded));
  return nullptr;
}

std::unique_ptr<Pcm> SoundCache::decodeUncached(const std::string& wanted, const std::string& fallback) {
  std::string path = wanted;
  if (!fallback.empty() && !bytes(wanted, true)) path = fallback;
  std::unique_ptr<Pcm> pcm;
  if (const Bytes* raw = bytes(path)) {
    DecodeError e = DecodeError::None;
    pcm = decodeBytes(*raw, &e);
    if (!pcm) {
      const char* why = e == DecodeError::UnsupportedRate       ? "sample rate outside 8000..96000 Hz"
                        : e == DecodeError::UnsupportedChannels ? "not mono or stereo"
                                                                : "cannot decode";
      logOnce(path, "[audio] rejected " + path + ": " + why);
    }
  }
  return pcm;
}

std::unique_ptr<OggStream> SoundCache::openStream(const std::string& wanted, const std::string& fallback) {
  std::string path = wanted;
  if (!fallback.empty() && !bytes(wanted, true)) path = fallback;
  const Bytes* raw = bytes(path);
  if (!raw) return nullptr;
  auto stream = OggStream::open(raw->data(), raw->size());
  if (!stream) logOnce(path, "[audio] rejected " + path + ": cannot stream");
  return stream;
}

void SoundCache::startPrefetch(std::vector<Request> list, std::function<void()> onStart) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& r : list) {
      // De-dup by primary only: the first request's fallback wins. Safe only because every primary exists in both
      // Reforged's cadi/audio and vanilla's CadSource (six sweetener primaries have two different fallbacks).
      if (r.primary.empty() || entries_.count(r.primary)) continue;
      order_.push_back(r.primary);
      entries_[r.primary].req = std::move(r);
    }
    if (order_.empty()) {
      finished_ = reported_ = true;
      return;
    }
  }
  worker_ = std::thread([this, onStart = std::move(onStart)] { prefetchLoop(onStart); });
}

// The worker: file reads and Vorbis decoding only. The loader is a read of the immutable asset index.
void SoundCache::prefetchLoop(std::function<void()> onStart) {
  if (onStart) onStart();
  const auto t0 = std::chrono::steady_clock::now();
  std::size_t count = 0, kb = 0;
  for (const std::string& key : order_) {  // order_ is never changed after startPrefetch
    Request req;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) break;
      Entry& e = entries_.at(key);
      if (e.state != Job::Queued) continue;  // pcm() took it
      e.state = Job::Running;
      req = e.req;
    }
    std::unique_ptr<Pcm> result;
    if (loader_) {
      auto raw = loader_(req.primary);
      if (!raw && !req.fallback.empty()) raw = loader_(req.fallback);
      if (raw) result = decodeBytes(*raw, nullptr);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      Entry& e = entries_.at(key);
      if (result) {
        ++count;
        kb += result->samples.size() * sizeof(int16_t) / 1024;
      }
      e.result = std::move(result);
      e.state = Job::Done;
    }
    cv_.notify_all();
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    prefetchedCount_ = count;
    prefetchedBytes_ = kb;
    prefetchMs_ = ms;
    finished_ = true;
  }
}

void SoundCache::pollPrefetch() {
  std::string line;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_ || reported_) return;
    reported_ = true;
    char buf[96];
    std::snprintf(buf, sizeof buf, "[audio] prefetched %zu sounds, %zu KB, in %.0f ms", prefetchedCount_, prefetchedBytes_,
                  prefetchMs_);
    line = buf;
  }
  if (log_) log_(line);
}

const Pcm* SoundCache::pcm(const std::string& wanted, const std::string& fallback) {
  if (auto it = pcm_.find(wanted); it != pcm_.end()) return it->second.get();
  if (!order_.empty()) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (auto it = entries_.find(wanted); it != entries_.end()) {
      Entry& e = it->second;
      if (e.state == Job::Queued) {
        e.state = Job::Taken;  // not started: decode it here rather than wait for the queue
      } else if (e.state != Job::Taken) {
        cv_.wait(lock, [&] { return e.state == Job::Done; });
        if (e.result) return (pcm_[wanted] = std::move(e.result)).get();
        e.state = Job::Taken;  // the worker could not read or decode it: fall through for the usual log line
      }
    }
  }
  std::string path = wanted;
  if (!fallback.empty() && !bytes(wanted, true)) path = fallback;
  auto it = pcm_.find(path);
  if (it != pcm_.end()) return it->second.get();
  return (pcm_[path] = decodeUncached(path)).get();
}

}  // namespace choco::audio
