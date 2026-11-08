#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "audio/Audio.h"
#include "audio/OggStream.h"
#include "audio/EventTable.h"
#include "games/choc3/AudioEvents.h"
#include "audio/Mixer.h"
#include "audio/Pcm.h"
#include "audio/SoundCache.h"

using namespace choco::audio;
namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

const fs::path kFixtures = fs::path(CHOCO_TEST_ASSETS) / "audio";

Pcm makePcm(int channels, std::vector<int16_t> samples) {
  Pcm p;
  p.channels = channels;
  p.samples = std::move(samples);
  return p;
}

Pcm constant(int16_t value, std::size_t frames) { return makePcm(1, std::vector<int16_t>(frames, value)); }

// Renders `frames` and returns the left channel; right is checked against left where the source is mono.
std::vector<float> renderLeft(Mixer& m, std::size_t frames) {
  std::vector<float> buf(frames * 2);
  m.render(buf.data(), frames);
  std::vector<float> left(frames);
  for (std::size_t i = 0; i < frames; ++i) left[i] = buf[i * 2];
  return left;
}

constexpr float kHalf = 16384.0f / 32768.0f;

}  // namespace

// ---- decoder ----

TEST_CASE("decodeOgg: a stereo 44.1 kHz fixture decodes with the right shape") {
  const auto bytes = readAll(kFixtures / "sine_stereo.ogg");
  REQUIRE(!bytes.empty());
  DecodeError e = DecodeError::Corrupt;
  const auto pcm = decodeOgg(bytes, &e);
  REQUIRE(pcm.has_value());
  CHECK(e == DecodeError::None);
  CHECK(pcm->channels == 2);
  CHECK(pcm->sampleRate == 44100);
  CHECK(static_cast<double>(pcm->frames()) == doctest::Approx(22050).epsilon(0.02));  // 0.5 s
  int peak = 0;
  for (int16_t s : pcm->samples) peak = std::max(peak, std::abs(static_cast<int>(s)));
  CHECK(peak > 1000);
}

TEST_CASE("decodeOgg: a 22.05 kHz file is resampled to 44100") {
  DecodeError e = DecodeError::None;
  const auto pcm = decodeOgg(readAll(kFixtures / "sine_22k.ogg"), &e);
  REQUIRE(pcm);
  CHECK(pcm->sampleRate == 44100);
  CHECK(pcm->frames() > 1000);
  // The length matches what the streaming resampler reports for the same file (twice the source frames).
  const auto bytes = readAll(kFixtures / "sine_22k.ogg");
  auto stream = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(stream);
  CHECK(pcm->frames() == stream->totalFrames());
  int peak = 0;
  for (int16_t v : pcm->samples) peak = std::max(peak, std::abs(static_cast<int>(v)));
  CHECK(peak > 1000);
}

TEST_CASE("decodeOgg: corrupt, truncated and empty input fail cleanly") {
  DecodeError e = DecodeError::None;
  CHECK(!decodeOgg(nullptr, 0, &e));
  CHECK(e == DecodeError::Corrupt);
  CHECK(!decodeOgg(std::vector<uint8_t>{}, &e));
  std::vector<uint8_t> junk(4096);
  for (std::size_t i = 0; i < junk.size(); ++i) junk[i] = static_cast<uint8_t>(i * 131 + 7);
  CHECK(!decodeOgg(junk, &e));
  CHECK(e == DecodeError::Corrupt);
  const auto good = readAll(kFixtures / "sine_stereo.ogg");
  REQUIRE(good.size() > 200);
  for (std::size_t cut : {std::size_t{4}, std::size_t{40}, std::size_t{100}, good.size() / 2}) {
    std::vector<uint8_t> part(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
    // A cut inside the stream may still yield the packets before it; it must never crash or return garbage sizes.
    const auto r = decodeOgg(part);
    if (r) CHECK(r->frames() <= 22100);
  }
  std::vector<uint8_t> broken = good;
  for (std::size_t i = 0; i < 80; ++i) broken[i] ^= 0xA5;  // the headers
  CHECK(!decodeOgg(broken));
}

TEST_CASE("SoundCache: decodes once, remembers failures and logs a missing file once") {
  int loads = 0;
  std::vector<std::string> lines;
  SoundCache cache(
      [&](const std::string& path) -> std::optional<SoundCache::Bytes> {
        ++loads;
        if (path == "good.ogg") return readAll(kFixtures / "sine_stereo.ogg");
        if (path == "bad.ogg") return SoundCache::Bytes(100, 7);
        return std::nullopt;
      },
      [&](const std::string& l) { lines.push_back(l); });
  const Pcm* a = cache.pcm("good.ogg");
  REQUIRE(a);
  CHECK(cache.pcm("good.ogg") == a);
  CHECK(loads == 1);
  CHECK(!cache.pcm("missing.ogg"));
  CHECK(!cache.pcm("missing.ogg"));
  CHECK(!cache.pcm("bad.ogg"));
  CHECK(!cache.pcm("bad.ogg"));
  CHECK(lines.size() == 2);
}

TEST_CASE("decodeOgg: every Reforged cadi file is 44.1 kHz mono or stereo") {
  const fs::path dir = fs::path(CHOCO_REFORGED_ASSETS) / "cadi";
  if (!fs::exists(dir / "audio")) {
    // Missing assets fail the test unless the run says they are optional (CHOCO_ASSETS_OPTIONAL=1), so a broken checkout
    // can't pass silently.
    const char* optional = std::getenv("CHOCO_ASSETS_OPTIONAL");
    if (optional && *optional && *optional != '0') {
      MESSAGE("SKIPPED: " << dir.string() << " not present (CHOCO_ASSETS_OPTIONAL)");
      return;
    }
    FAIL(dir.string() << " is missing; link Chocolatier-Decadence-Reforged or set CHOCO_ASSETS_OPTIONAL=1");
  }
  int n = 0, mono = 0;
  for (const auto& e : fs::recursive_directory_iterator(dir)) {
    if (e.path().extension() != ".ogg") continue;
    const auto pcm = decodeOgg(readAll(e.path()));
    INFO(e.path().string());
    REQUIRE(pcm.has_value());
    CHECK(pcm->sampleRate == 44100);
    mono += pcm->channels == 1;
    ++n;
  }
  CHECK(n == 165);  // 164 in cadi/audio + ui_click
  CHECK(mono > 0);
}

// ---- mixer maths ----

TEST_CASE("Mixer: a one-shot renders sample x all gains exactly and frees at its end") {
  const Pcm pcm = makePcm(1, {16384, -16384, 8192});
  Mixer m;
  m.setGroupGain(Group::Sfx, 0.5f);
  m.setMaster(1.0f);
  VoiceParams p;
  p.gain = 0.5f;
  REQUIRE(m.play(&pcm, p) == 0);
  std::vector<float> buf(8, 9.0f);
  m.render(buf.data(), 2);
  CHECK(buf[0] == doctest::Approx(0.5f * 0.5f * 0.5f));
  CHECK(buf[1] == buf[0]);  // mono feeds both channels
  CHECK(buf[2] == doctest::Approx(-0.5f * 0.5f * 0.5f));
  CHECK(m.activeVoices() == 1);
  m.render(buf.data(), 1);
  CHECK(buf[0] == doctest::Approx(0.25f * 0.25f));
  CHECK(m.activeVoices() == 0);
  m.render(buf.data(), 2);
  CHECK(buf[0] == 0.0f);  // render overwrites
}

TEST_CASE("Mixer: master, group gains and the hard clip") {
  const Pcm loud = constant(26214, 4);  // 0.8
  Mixer m;
  VoiceParams p;
  m.play(&loud, p);
  m.play(&loud, p);
  auto out = renderLeft(m, 4);
  CHECK(out[0] == 1.0f);  // 1.6 clipped
  Mixer neg;
  const Pcm negative = constant(-26214, 4);
  neg.play(&negative, p);
  neg.play(&negative, p);
  CHECK(renderLeft(neg, 1)[0] == -1.0f);

  Mixer g;
  g.play(&loud, VoiceParams{Group::Env, true, 1.0f, false, -1, 0});
  g.setGroupGain(Group::Env, 0.5f);
  g.setGroupGain(Group::Sfx, 0.0f);
  CHECK(renderLeft(g, 1)[0] == doctest::Approx(0.4f).epsilon(1e-4));
  g.setMaster(0.0f);
  CHECK(renderLeft(g, 1)[0] == 0.0f);
}

TEST_CASE("Mixer: stereo keeps its channels apart") {
  const Pcm st = makePcm(2, {16384, -16384});
  Mixer m;
  m.play(&st, VoiceParams{});
  float buf[2];
  m.render(buf, 1);
  CHECK(buf[0] == doctest::Approx(0.5f));
  CHECK(buf[1] == doctest::Approx(-0.5f));
}

TEST_CASE("Mixer: a loop wraps sample-exactly across render calls") {
  const Pcm pcm = makePcm(1, {1000, 2000, 3000});
  Mixer m;
  VoiceParams p;
  p.loop = true;
  m.play(&pcm, p);
  std::vector<float> all;
  for (std::size_t n : {std::size_t{2}, std::size_t{5}, std::size_t{4}}) {
    const auto part = renderLeft(m, n);
    all.insert(all.end(), part.begin(), part.end());
  }
  REQUIRE(all.size() == 11);
  for (std::size_t i = 0; i < all.size(); ++i) CHECK(all[i] == doctest::Approx(pcm.samples[i % 3] / 32768.0f));
  CHECK(m.activeVoices() == 1);
}

TEST_CASE("Mixer: a stereo loop wraps sample-exactly with both channels kept apart") {
  const Pcm pcm = makePcm(2, {1000, -1000, 2000, -2000, 3000, -3000});
  Mixer m;
  VoiceParams p;
  p.loop = true;
  m.play(&pcm, p);
  std::vector<float> all;
  for (std::size_t n : {std::size_t{2}, std::size_t{3}, std::size_t{3}}) {
    std::vector<float> buf(n * 2);
    m.render(buf.data(), n);
    all.insert(all.end(), buf.begin(), buf.end());
  }
  REQUIRE(all.size() == 16);
  for (std::size_t i = 0; i < all.size(); ++i) CHECK(all[i] == doctest::Approx(pcm.samples[i % 6] / 32768.0f));
}

TEST_CASE("Mixer: fade-in and fade-out take exactly the given milliseconds") {
  CHECK(msToFrames(1) == 44);
  const Pcm pcm = constant(16384, 1000);
  Mixer m;
  VoiceParams p;
  p.loop = true;
  p.fadeInMs = 1;  // 44 frames
  m.play(&pcm, p);
  const auto in = renderLeft(m, 60);
  CHECK(in[0] == 0.0f);
  CHECK(in[22] == doctest::Approx(kHalf * 0.5f));
  CHECK(in[43] == doctest::Approx(kHalf * 43.0f / 44.0f));
  CHECK(in[44] == doctest::Approx(kHalf));
  CHECK(in[59] == doctest::Approx(kHalf));

  m.release(-1, 1);  // no voice has tag -1 ... except this one: untagged voices carry -1
  const auto out = renderLeft(m, 60);
  CHECK(out[0] == doctest::Approx(kHalf));
  CHECK(out[22] == doctest::Approx(kHalf * 0.5f));
  CHECK(out[43] > 0.0f);
  CHECK(out[44] == 0.0f);
  CHECK(m.activeVoices() == 0);  // freed once the 44 frames of the ramp ran out
}

TEST_CASE("Mixer: release with 0 ms frees at once, and only the tagged voices") {
  const Pcm pcm = constant(100, 100);
  Mixer m;
  VoiceParams a;
  a.loop = true;
  a.tag = 3;
  VoiceParams b = a;
  b.tag = 4;
  m.play(&pcm, a);
  m.play(&pcm, b);
  CHECK(m.hasTag(3));
  m.release(3, 0);
  CHECK(!m.hasTag(3));
  CHECK(m.hasTag(4));
  CHECK(m.activeVoices() == 1);
}

TEST_CASE("Mixer: the 65th voice is dropped, and a freed slot is reusable") {
  const Pcm pcm = constant(100, 100);
  Mixer m;
  VoiceParams p;
  p.loop = true;
  p.tag = 1;
  for (int i = 0; i < kMaxVoices; ++i) REQUIRE(m.play(&pcm, p) == i);
  CHECK(m.play(&pcm, p) == -1);
  CHECK(m.dropped() == 1);
  CHECK(m.activeVoices() == 64);
  m.stop(10);
  CHECK(m.play(&pcm, p) == 10);
}

TEST_CASE("Mixer: systemPaused freezes everything and cadiPaused freezes only CADI voices and tracks") {
  const Pcm pcm = makePcm(1, {1000, 2000, 3000, 4000});
  Mixer m;
  VoiceParams plain;
  VoiceParams cadi;
  cadi.cadi = true;
  m.play(&pcm, plain);  // slot 0
  m.play(&pcm, cadi);   // slot 1
  MusicTrack t;
  t.stages.push_back({{&pcm}, true});
  REQUIRE(m.startTrack(&t, 0));

  m.setCadiPaused(true);
  auto out = renderLeft(m, 2);
  CHECK(out[0] == doctest::Approx(1000 / 32768.0f));  // only the non-CADI voice
  CHECK(out[1] == doctest::Approx(2000 / 32768.0f));
  CHECK(m.voice(1).pos == 0);
  CHECK(t.timeline == 0);
  m.setSystemPaused(true);
  out = renderLeft(m, 2);
  CHECK(out[0] == 0.0f);
  CHECK(m.voice(0).pos == 2);  // position held
  m.setSystemPaused(false);
  m.setCadiPaused(false);
  out = renderLeft(m, 1);
  // slot 0 is at its 3rd frame (3000), slot 1 and the track at their first (1000 each).
  CHECK(out[0] == doctest::Approx((3000 + 1000 + 1000) / 32768.0f));
}

TEST_CASE("Mixer: a sting hands over to looping layers on the exact next frame") {
  const Pcm sting = makePcm(1, {100, 200, 300, 400, 500});
  const Pcm layerA = makePcm(1, {1000, 2000, 3000});
  const Pcm layerB = makePcm(1, {10, 20, 30});
  MusicTrack t;
  t.stages.push_back({{&sting}, false});
  t.stages.push_back({{&layerA, &layerB}, true, 5});  // enters on frame 5, where the sting ends
  Mixer m;
  REQUIRE(m.startTrack(&t, 0));
  const auto out = renderLeft(m, 14);
  const float expect[14] = {100, 200, 300, 400, 500, 1010, 2020, 3030, 1010, 2020, 3030, 1010, 2020, 3030};
  for (int i = 0; i < 14; ++i) CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(expect[i] / 32768.0f));
  CHECK(t.phase() == std::make_pair<std::size_t, std::size_t>(2, 2));
  CHECK(!t.done);
}

TEST_CASE("Mixer: stages sit on a timeline, with a gap, an overlap and a cut length") {
  const Pcm sting = makePcm(1, {100, 100, 100});
  const Pcm under = makePcm(1, {10, 20, 30, 40, 50, 60});
  const Pcm layer = makePcm(1, {1000, 2000});
  MusicTrack t;
  t.stages.push_back({{&sting}, false, 0, 0});
  t.stages.push_back({{&under}, false, 0, 4});  // cut to 4 frames although the file has 6
  t.stages.push_back({{&layer}, false, 6, 0});  // 2 silent frames (4, 5) between
  Mixer m;
  REQUIRE(m.startTrack(&t, 0));
  CHECK(t.phase() == std::make_pair<std::size_t, std::size_t>(1, 2));
  const auto out = renderLeft(m, 10);
  const float expect[10] = {110, 120, 130, 40, 0, 0, 1000, 2000, 0, 0};
  for (int i = 0; i < 10; ++i) CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(expect[i] / 32768.0f));
  CHECK(t.done);  // the last stage ended on frame 8
}

TEST_CASE("Mixer: endFrames ends a clip early; loopFrames wraps the whole timeline") {
  const Pcm a = makePcm(1, {1, 2, 3, 4, 5, 6, 7, 8});
  const Pcm b = makePcm(1, {100, 200});
  {
    MusicTrack t;
    t.stages.push_back({{&a}, true});
    t.endFrames = 5;
    Mixer m;
    REQUIRE(m.startTrack(&t, 0));
    const auto out = renderLeft(m, 8);
    for (int i = 0; i < 5; ++i) CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(static_cast<float>(i + 1) / 32768.0f));
    CHECK(out[5] == 0.0f);
    CHECK(t.done);
  }
  {
    MusicTrack t;
    t.stages.push_back({{&a}, false});
    t.stages.push_back({{&b}, false, 3});
    t.loopFrames = 6;  // the clip is 6 frames long and loops, so the 8-frame file is cut and b enters every pass
    Mixer m;
    REQUIRE(m.startTrack(&t, 0));
    const auto out = renderLeft(m, 14);
    const float expect[14] = {1, 2, 3, 104, 205, 6, 1, 2, 3, 104, 205, 6, 1, 2};
    for (int i = 0; i < 14; ++i) CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(expect[i] / 32768.0f));
    CHECK(!t.done);
  }
}

TEST_CASE("Mixer: startTrack rejects a stage with more than nine sources, an empty stage and a null source") {
  const Pcm p = constant(100, 10);
  Mixer m;
  MusicTrack ten;
  MusicStage st;
  st.sources.assign(10, &p);
  ten.stages.push_back(st);
  CHECK(!m.startTrack(&ten, 0));
  MusicTrack nine;
  st.sources.resize(kMaxStemsPerStage);
  nine.stages.push_back(st);
  CHECK(m.startTrack(&nine, 0));
  MusicTrack empty;
  empty.stages.push_back({});
  CHECK(!m.startTrack(&empty, 0));
  MusicTrack nul;
  nul.stages.push_back({{Source()}, false});
  CHECK(!m.startTrack(&nul, 0));
}

TEST_CASE("Mixer: a non-looping stage waits for its longest source, and the last stage ends the track") {
  const Pcm shortOne = makePcm(1, {1, 1});
  const Pcm longOne = makePcm(1, {2, 2, 2, 2});
  const Pcm after = makePcm(1, {1000});
  MusicTrack t;
  t.stages.push_back({{&shortOne, &longOne}, false});
  t.stages.push_back({{&after}, false, 4});
  Mixer m;
  REQUIRE(m.startTrack(&t, 0));
  const auto out = renderLeft(m, 7);
  const float expect[7] = {3, 3, 2, 2, 1000, 0, 0};
  for (int i = 0; i < 7; ++i) CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(expect[i] / 32768.0f));
  CHECK(t.done);
  MusicTrack* reaped[4];
  CHECK(m.reap(reaped, 4) == 1);
  CHECK(reaped[0] == &t);
  CHECK(m.current() == nullptr);
}

TEST_CASE("Mixer: a crossfade sums the fading and the new track mid-fade, and the old one is reaped after") {
  const Pcm a = constant(16384, 500);  // 0.5
  const Pcm b = constant(8192, 500);   // 0.25
  MusicTrack ta, tb;
  ta.stages.push_back({{&a}, true});
  tb.stages.push_back({{&b}, true});
  Mixer m;
  REQUIRE(m.startTrack(&ta, 0));
  CHECK(renderLeft(m, 1)[0] == doctest::Approx(0.5f));
  REQUIRE(m.startTrack(&tb, 1));  // 44 frames
  CHECK(m.fading() == &ta);
  const auto out = renderLeft(m, 50);
  for (int k : {0, 11, 22, 43}) {
    const float x = static_cast<float>(k) / 44.0f;
    CHECK(out[static_cast<std::size_t>(k)] == doctest::Approx(0.5f * (1 - x) + 0.25f * x));
  }
  CHECK(out[44] == doctest::Approx(0.25f));
  CHECK(out[49] == doctest::Approx(0.25f));
  MusicTrack* reaped[4];
  REQUIRE(m.reap(reaped, 4) == 1);
  CHECK(reaped[0] == &ta);
  CHECK(m.fading() == nullptr);
  CHECK(m.current() == &tb);
}

TEST_CASE("Mixer: a third track during a fade cuts the one fading out; stopTrack fades or cuts") {
  const Pcm p = constant(16384, 500);
  MusicTrack t1, t2, t3;
  for (MusicTrack* t : {&t1, &t2, &t3}) t->stages.push_back({{&p}, true});
  Mixer m;
  m.startTrack(&t1, 0);
  m.startTrack(&t2, 10);
  CHECK(m.fading() == &t1);
  m.startTrack(&t3, 10);
  CHECK(m.fading() == &t2);
  CHECK(t1.done);
  MusicTrack* reaped[4];
  CHECK(m.reap(reaped, 4) == 1);
  CHECK(reaped[0] == &t1);

  m.stopTrack(1);
  CHECK(m.current() == nullptr);
  CHECK(m.fading() == &t3);
  CHECK(t2.done);  // the earlier fade is cut
  renderLeft(m, 44);
  CHECK(t3.done);

  MusicTrack t4;
  t4.stages.push_back({{&p}, true});
  Mixer m2;
  m2.startTrack(&t4, 0);
  m2.stopTrack(0);
  CHECK(t4.done);
  CHECK(m2.current() == nullptr);
  CHECK(renderLeft(m2, 2)[0] == 0.0f);
}

TEST_CASE("Mixer: a track with no stages is refused") {
  MusicTrack t;
  Mixer m;
  CHECK(!m.startTrack(&t, 0));
  CHECK(!m.startTrack(nullptr, 0));
}

// ---- event table ----

TEST_CASE("EventTable: lookup is exact, case-sensitive, and unknown names are null") {
  const EventTable& t = choco::games::choc3::audioEvents();
  CHECK(t.lookup("buy"));
  CHECK(t.lookup("buy")->kind == Kind::Oneshot);
  CHECK(!t.lookup("Buy"));
  CHECK(!t.lookup("buy "));
  CHECK(!t.lookup("no_such_event"));
  CHECK(!t.lookup("main_menu_x"));  // no prefix matching
  CHECK(!t.lookup("cadi/ui_click.ogg") == false);
  CHECK(t.lookup("cadi/ui_click.ogg")->kind == Kind::Silent);
  CHECK(t.lookup("ui_click")->kind == Kind::Direct);
  CHECK(t.lookup("Pause")->kind == Kind::Builtin);
  CHECK(t.lookup("Music Volume")->kind == Kind::Volume);
  CHECK(t.lookup("Stop_conveyor_belt_loop")->kind == Kind::Silent);
}

TEST_CASE("EventTable: values from the tsv reach the table") {
  const EventTable& t = choco::games::choc3::audioEvents();
  const EventDef* tp = t.lookup("travel_plane");
  REQUIRE(tp);
  CHECK(tp->kind == Kind::Loop);
  CHECK(tp->attackMs == 1601);
  CHECK(tp->releaseMs == 1601);
  const EventDef* z = t.lookup("baghdad");
  REQUIRE(z);
  CHECK(z->kind == Kind::AmbienceLoop);
  CHECK(z->group == Group::Env);
  CHECK(z->intervalMinMs == 1140);
  CHECK(z->intervalMaxMs == 5140);
  CHECK(z->sweeteners.size() == 5);
  CHECK(t.lookup("crate_with_money")->channelVolume == doctest::Approx(1.02436f));
  CHECK(t.lookup("buy")->channelVolume == 1.0f);
  CHECK(t.lookup("map_")->fadeMs == 2210);
  CHECK(t.lookup("tangiers_")->srcFadeMs == 1000);
  CHECK(t.lookup("main_menu_")->srcFadeMs == -1);
  CHECK(t.lookup("choco_making_game")->channelVolume == doctest::Approx(1.352952f));
}

TEST_CASE("EventTable: launch is one random pick out of its 10 files (AU s.10.6)") {
  const EventDef* l = choco::games::choc3::audioEvents().lookup("launch");
  REQUIRE(l);
  CHECK(l->kind == Kind::OneshotRandom);
  CHECK(l->files.size() == 10);
  CHECK(l->channelVolume == doctest::Approx(0.41622f));
}

TEST_CASE("EventTable: aliases resolve to an arrangement and part, and every one targets a real arrangement") {
  const EventTable& t = choco::games::choc3::audioEvents();
  Resolved r = t.resolve("main_menu");
  REQUIRE(r.def);
  CHECK(r.alias);
  CHECK(r.def->name == "main_menu_");
  CHECK(r.part == Part::Start);
  CHECK(t.resolve("coffee_making_game_intro").part == Part::Intro);
  CHECK(t.resolve("chocolate_making_game_outro").part == Part::Outro);
  CHECK(t.resolve("chocolate_making_game").def->name == "choco_making_game");
  CHECK(t.resolve("falklands_sting").def->name == "falklands_");
  CHECK(t.resolve("gobidesert_sting").def->name == "gobi");
  CHECK(t.resolve("morocco_sting").def->name == "tangiers_");
  CHECK(t.resolve("kona_sting").def->name == "Kona_");
  CHECK(t.resolve("port_music2").def->name == "C3 Gameplay 2");
  CHECK(t.resolve("port_music2").masterFadeMs == 2760);
  CHECK(t.resolve("port_music2").sourceFadeMs == 2000);
  CHECK(t.resolve("coffee_making_game").masterFadeMs == 1160);
  CHECK(t.resolve("coffee_making_game").sourceFadeMs == -1);
  // Exact names only (AU s.10.2): the vanilla spellings, case variants and the missing coffee outro match nothing.
  for (const char* silent : {"gobi_desert_sting", "falkland_islands_sting", "coffee_making_game_outro", "saloons",
                             "train_station", "Kona_sting", "Main_Menu", "map_screen_x", "lima_"})
    CHECK(!t.resolve(silent).alias);
  CHECK(!t.lookup("gobi_desert_sting"));
  CHECK(!t.lookup("coffee_making_game_outro"));
  CHECK(t.lookup("lima_"));  // the channel itself is also an exact event (AU s.10.2)
  CHECK(t.resolve("buy").alias == false);
  for (const auto& a : t.aliases()) {
    INFO(a.event);
    const EventDef* arr = t.arrangement(a.arrangement);
    REQUIRE(arr);
    CHECK(t.resolve(a.event).def == arr);
    CHECK(t.lookup(a.event) == arr);
  }
}

TEST_CASE("EventTable: music roles come from music.tsv and use only the arrangement's own files") {
  const EventTable& t = choco::games::choc3::audioEvents();
  int arrangements = 0;
  for (const EventDef& d : t.events()) {
    if (d.kind != Kind::Music) continue;
    ++arrangements;
    INFO(d.name);
    REQUIRE(d.music.present);
    std::vector<std::string> used = d.music.layers;
    for (const std::string* f : {&d.music.sting, &d.music.intro, &d.music.body, &d.music.ending})
      if (!f->empty()) used.push_back(*f);
    CHECK(!used.empty());
    for (const std::string& f : used) CHECK(std::count(d.files.begin(), d.files.end(), f) == 1);
    CHECK(d.fadeMs > 0);
    for (const std::string& f : used) CHECK(f != "portmusicFADE.ogg");
  }
  CHECK(arrangements == 27);
  const EventDef* choco = t.lookup("choco_making_game");
  CHECK(choco->music.intro == "C3_Minigame1_Intro.ogg");
  CHECK(choco->music.body == "C3_Minigame1_Bpdy_60sec.ogg");
  CHECK(choco->music.ending == "C3_Mini1_NEWENDING3-TiSh_02.ogg");
  CHECK(t.lookup("belize_")->music.sting == "STING_Belize.ogg");
  CHECK(t.lookup("las_vegas_")->music.sting == "STING_LasVegas.ogg");
  CHECK(t.lookup("map_")->music.layers.size() == 2);
  CHECK(t.lookup("main_menu_")->music.body == "Main_Menu_v2.ogg");
}

TEST_CASE("EventTable: every Stop_ event has a target that exists") {
  const EventTable& t = choco::games::choc3::audioEvents();
  int stops = 0;
  for (const EventDef& d : t.events()) {
    if (d.name.rfind("Stop_", 0) != 0 || d.kind == Kind::Silent) continue;
    ++stops;
    INFO(d.name);
    CHECK(d.kind == Kind::Stop);
    if (d.stopGroup == StopGroup::None) {
      REQUIRE(d.stopTarget >= 0);
      CHECK("Stop_" + t.events()[static_cast<std::size_t>(d.stopTarget)].name == d.name);
    }
  }
  CHECK(stops == 5);
}

TEST_CASE("EventTable: every row of audio-events.tsv is in the table") {
  std::ifstream f(fs::path(CHOCO_RE_DOCS) / "audio-events.tsv");
  REQUIRE(f.good());
  std::string line;
  std::getline(f, line);
  int rows = 0;
  const EventTable& t = choco::games::choc3::audioEvents();
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    const std::string name = line.substr(0, line.find('\t'));
    ++rows;
    INFO(name);
    const Resolved r = t.resolve(name);
    // The old inferred aliases are superseded by the exact clip names; those that are not clips are silent.
    if (line.find("\talias_inferred\t") != std::string::npos) {
      const bool clip = name != "gobi_desert_sting" && name != "falkland_islands_sting" && name != "coffee_making_game_outro";
      CHECK((r.def != nullptr) == clip);
      CHECK(r.alias == clip);
      continue;
    }
    CHECK(r.def != nullptr);
  }
  CHECK(rows > 150);
}

// ---- Audio service ----

namespace {

struct Harness {
  std::vector<std::string> lines;
  std::vector<std::string> requested;  // every path the loader was asked for
  std::function<bool(const std::string&)> exists;  // empty = every path exists
  Audio audio;
  explicit Harness(uint32_t seed = 1, std::function<bool(const std::string&)> existsFn = {})
      : exists(std::move(existsFn)),
        audio(
            [this](const std::string& path) -> std::optional<SoundCache::Bytes> {
              requested.push_back(path);
              if (exists && !exists(path)) return std::nullopt;
              return readAll(kFixtures / "sine_stereo.ogg");
            },
            choco::games::choc3::audioEvents(), [this](const std::string& l) { lines.push_back(l); }, seed) {}
  int count(const std::string& needle) const {
    return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; }));
  }
};

}  // namespace

TEST_CASE("Audio: an unknown event name reports once and is silent; known-silent names report nothing") {
  Harness h;
  h.audio.event("nonsense");
  h.audio.event("nonsense");
  h.audio.event("Nonsense");
  h.audio.event("Music Volume");
  h.audio.event("Stop_conveyor_belt_loop");
  h.audio.event("cadi/ui_click.ogg");
  CHECK(h.count("unknown event nonsense") == 1);
  CHECK(h.count("unknown event Nonsense") == 1);
  CHECK(h.lines.size() == 2);
  CHECK(h.audio.mixer().activeVoices() == 0);
}

TEST_CASE("Audio: one-shots add voices, layered events play one file per layer, direct names take the direct path") {
  Harness h;
  h.audio.event("buy");
  h.audio.event("buy");
  CHECK(h.audio.mixer().activeVoices() == 2);
  h.audio.event("launch");  // one random child, not three layers
  CHECK(h.audio.mixer().activeVoices() == 3);
  h.audio.event("launch");
  CHECK(h.audio.mixer().activeVoices() == 4);
  CHECK(h.count("launch -> random") == 2);
  {
    // Never the same child twice in a row (a pause between triggers lets the 0.5 s fixture finish).
    Harness r;
    std::string prev;
    for (int i = 0; i < 40; ++i) {
      r.audio.update(i * 1000.0);
      r.audio.event("launch");
      const std::string& l = r.lines.back();
      CHECK(l != prev);
      prev = l;
    }
  }
  h.audio.event("ui_click");
  h.audio.event("firework3");
  CHECK(h.audio.mixer().activeVoices() == 6);
  CHECK(h.count("firework3 -> direct sfx/firework3.ogg") == 1);
  CHECK(h.audio.mixer().voice(4).group == Group::Direct);
  CHECK(!h.audio.mixer().voice(4).cadi);
  CHECK(h.audio.mixer().voice(0).cadi);
  h.audio.playFile("sfx/sfx_typing_01.ogg?group=1000");
  h.audio.playFile("sfx/x.ogg?group=1001");
  CHECK(h.audio.mixer().voice(6).group == Group::Direct);
  CHECK(h.audio.mixer().voice(7).group == Group::Music);
  const std::string d = h.audio.dump();
  CHECK(d.find("buy=2") != std::string::npos);
  CHECK(d.find("cadi/ui_click.ogg=1") != std::string::npos);
  CHECK(d.find("sfx/firework3.ogg=1") != std::string::npos);
  CHECK(d.find("launch=2") != std::string::npos);
}

TEST_CASE("Audio: ui_click and the fireworks log, count and play exactly as their table rows say") {
  Harness h;
  for (const char* n : {"ui_click", "firework1", "firework3", "firework5"}) h.audio.event(n);
  h.audio.event("ui_click");
  const std::vector<std::string> expected = {
      "[audio] ui_click -> direct cadi/ui_click.ogg", "[audio] firework1 -> direct sfx/firework1.ogg",
      "[audio] firework3 -> direct sfx/firework3.ogg", "[audio] firework5 -> direct sfx/firework5.ogg",
      "[audio] ui_click -> direct cadi/ui_click.ogg"};
  CHECK(h.lines == expected);
  CHECK(h.audio.mixer().activeVoices() == 5);
  for (int i = 0; i < 5; ++i) {
    CHECK(h.audio.mixer().voice(i).group == Group::Direct);
    CHECK(!h.audio.mixer().voice(i).cadi);
  }
  CHECK(h.audio.dump() ==
        "audio levels sfx=0.750 music=0.650 env=0.500 mute=0 pause=none\n"
        "audio music none\n"
        "audio env none\n"
        "audio fired cadi/ui_click.ogg=2 sfx/firework1.ogg=1 sfx/firework3.ogg=1 sfx/firework5.ogg=1\n");
}

TEST_CASE("Audio: loops restart, Stop_ releases over the envelope, environments exclude each other") {
  Harness h;
  h.audio.event("machine_spin");
  h.audio.event("machine_spin");
  CHECK(h.audio.mixer().activeVoices() == 1);
  CHECK(h.audio.dump().find("audio loop machine_spin\n") != std::string::npos);
  h.audio.event("Stop_machine_spin");
  CHECK(h.audio.mixer().activeVoices() == 0);
  h.audio.event("travel_plane");
  h.audio.event("Stop_travel_plane");  // 1601 ms release
  CHECK(h.audio.mixer().activeVoices() == 1);
  h.audio.update(0);
  h.audio.update(1000);
  CHECK(h.audio.mixer().activeVoices() == 1);
  h.audio.update(1700);
  CHECK(h.audio.mixer().activeVoices() == 0);

  Harness e;
  e.audio.event("zurich");
  e.audio.event("zurich");
  CHECK(e.count("zurich -> environment ignored") == 1);
  CHECK(e.audio.dump().find("audio env zurich\n") != std::string::npos);
  e.audio.event("tokyo");
  CHECK(e.audio.dump().find("audio env tokyo\n") != std::string::npos);
  CHECK(e.audio.mixer().hasTag(e.audio.table().resolve("tokyo").index));
  e.audio.event("Stop_Environments");
  CHECK(e.audio.dump().find("audio env none\n") != std::string::npos);
}

TEST_CASE("Audio: sweeteners are deterministic for a seed") {
  std::string sigs[3];
  Harness a(7), b(7), c(8);
  Harness* hs[3] = {&a, &b, &c};
  for (int i = 0; i < 3; ++i) {
    Harness& h = *hs[i];
    h.audio.event("bali");
    h.audio.update(0);
    for (int ms = 100; ms <= 20000; ms += 100) {
      h.audio.update(ms);
      sigs[i] += std::to_string(h.audio.mixer().activeVoices());
      for (int v = 1; v < kMaxVoices; ++v)
        if (h.audio.mixer().voice(v).active) sigs[i] += "@" + std::to_string(h.audio.mixer().voice(v).pos) + "g" + std::to_string(h.audio.mixer().voice(v).gain);
      sigs[i] += ";";
    }
  }
  CHECK(sigs[0] == sigs[1]);
  CHECK(sigs[0] != sigs[2]);
  CHECK(sigs[0].find("@") != std::string::npos);  // sweeteners did fire
  CHECK(a.audio.mixer().framesRendered() == 882000);  // 20 s at 44.1 kHz, pumped from the clock
}

TEST_CASE("Audio: sweeteners wait out a pause (audio time stands still)") {
  // Most voice counts: polls every 50 ms and returns the highest number of live voices seen.
  auto mostVoices = [](Harness& h, int fromMs, int toMs) {
    int most = 0;
    for (int ms = fromMs; ms <= toMs; ms += 50) {
      h.audio.update(ms);
      most = std::max(most, h.audio.mixer().activeVoices());
    }
    return most;
  };
  Harness control(7);  // no pause: sweeteners (interval from 1.1 s) are heard within the first 10 s
  control.audio.event("bali");
  control.audio.update(0);
  CHECK(mostVoices(control, 50, 10000) >= 2);

  Harness p(7);
  p.audio.event("bali");
  p.audio.update(0);
  p.audio.setForeground(false);
  CHECK(mostVoices(p, 50, 10000) == 1);  // the proof: only the bed in 10 paused seconds
  p.audio.setForeground(true);
  CHECK(mostVoices(p, 10050, 20000) >= 2);  // and they start once audio time moves
}

TEST_CASE("Audio: pause state, levels and the dump format") {
  Harness h;
  CHECK(h.audio.dump().find("audio levels sfx=0.750 music=0.650 env=0.500 mute=0 pause=none\n") == 0);
  h.audio.setForeground(false);
  CHECK(h.audio.dump().find("pause=focus\n") != std::string::npos);
  h.audio.event("Pause");
  CHECK(h.audio.dump().find("pause=focus+game\n") != std::string::npos);
  h.audio.setForeground(true);  // regaining focus does not undo a game pause
  CHECK(h.audio.dump().find("pause=game\n") != std::string::npos);
  CHECK(h.audio.mixer().cadiPaused());
  CHECK(!h.audio.mixer().systemPaused());
  h.audio.event("Unpause");
  CHECK(h.audio.dump().find("pause=none\n") != std::string::npos);
  h.audio.setLevels(0.5f, 0.25f, 1.0f, true);
  CHECK(h.audio.dump().find("sfx=0.500 music=0.250 env=1.000 mute=1") != std::string::npos);
  CHECK(h.audio.mixer().master() == 0.0f);
  CHECK(h.audio.mixer().groupGain(Group::Direct) == 0.5f);
  // AU s.10.6: group gain is the level itself, linear: no root gain, no Master Bus.
  CHECK(h.audio.mixer().groupGain(Group::Sfx) == 0.5f);
  CHECK(h.audio.mixer().groupGain(Group::Music) == 0.25f);
  CHECK(h.audio.mixer().groupGain(Group::Env) == 1.0f);
  h.audio.setLevels(2.0f, 1.0f, 0.3f, false);
  CHECK(h.audio.mixer().groupGain(Group::Sfx) == 2.0f);
  CHECK(h.audio.mixer().groupGain(Group::Direct) == 1.0f);  // TSound clamps
  CHECK(h.audio.mixer().groupGain(Group::Env) == 0.3f);
  CHECK(h.audio.dump().find("audio music none\naudio env none\n") != std::string::npos);
}

TEST_CASE("Audio: a voice renders at channelVolume x group level x master, with no root gain") {
  Harness h;
  h.audio.setLevels(0.5f, 0.65f, 0.5f, false);
  h.audio.event("launch");  // channel volume 0.41622
  const Voice& v = h.audio.mixer().voice(0);
  CHECK(v.gain == doctest::Approx(0.41622f));
  CHECK(h.audio.mixer().groupGain(v.group) == 0.5f);
}

TEST_CASE("Audio: music events build tracks, the same event twice does not rebuild, a new one crossfades") {
  Harness h;
  h.audio.event("map_screen");
  REQUIRE(h.audio.mixer().current());
  CHECK(h.audio.mixer().current()->stages.size() == 1);  // map layers loop together
  CHECK(h.audio.dump().find("audio music map_ stage=1/1\n") != std::string::npos);
  MusicTrack* first = h.audio.mixer().current();
  h.audio.event("map_screen");
  h.audio.event("map_");
  CHECK(h.audio.mixer().current() == first);
  CHECK(h.count("ignored") == 2);

  h.audio.event("zurich_sting");  // sting, then layers
  REQUIRE(h.audio.mixer().current());
  CHECK(h.audio.mixer().current() != first);
  CHECK(h.audio.mixer().fading() == first);
  CHECK(h.audio.mixer().current()->stages.size() == 4);  // sting, percussive under it, layers, portmusicFADE
  CHECK(h.audio.dump().find("audio music Zurich_ stage=1/3\n") != std::string::npos);
  CHECK(h.count("zurich_sting -> music Zurich_ (alias) fade 3320/1490") == 1);

  h.audio.event("Stop_Music");
  CHECK(h.audio.dump().find("audio music none\n") != std::string::npos);
  h.audio.update(0);
  h.audio.update(4000);  // past the 3320 ms fade
  CHECK(h.audio.mixer().current() == nullptr);
  CHECK(h.audio.mixer().fading() == nullptr);
}

TEST_CASE("Audio: a minigame start arriving during the intro is ignored; the chocolate outro replaces the body") {
  Harness h;
  h.audio.event("coffee_making_game_intro");
  CHECK(h.audio.mixer().current()->stages.size() == 2);  // intro once, then the body loops
  MusicTrack* t = h.audio.mixer().current();
  h.audio.event("coffee_making_game");
  CHECK(h.audio.mixer().current() == t);
  h.audio.event("coffee_making_game_outro");  // no such clip in the CAD: silent, nothing changes
  CHECK(h.audio.mixer().current() == t);
  CHECK(h.count("unknown event coffee_making_game_outro") == 1);
  h.audio.event("chocolate_making_game_outro");
  CHECK(h.audio.mixer().current() != t);
  CHECK(h.audio.mixer().current()->stages.size() == 1);
}

// ---- resources, timing and the fixes of the second round ----

TEST_CASE("EventTable: port arrangements name the exact Ce_MapMELODICLAYER file (AU s.10.9)") {
  const EventTable& t = choco::games::choc3::audioEvents();
  // The 16 arrangements whose LoopBox is the .ogg-named copy use Reforged's "(2)" file; the four with the .wav name don't.
  for (const char* n : {"map_", "Kona_", "Zurich_", "baghdad_", "bali_", "capetown_", "douala_", "gobi", "havana_", "lima_",
                        "san_francisco_", "tangiers_", "tokyo_", "toronto_", "uluru_", "wellington_"}) {
    INFO(n);
    const Resource* r = t.lookup(n)->resourceOf("Ce_MapMELODICLAYER.ogg");
    REQUIRE(r);
    CHECK(r->primary == "cadi/audio/Ce_MapMELODICLAYER (2).ogg");
    CHECK(r->fallback.rfind("cadi/cad/Resources/", 0) == 0);
  }
  for (const char* n : {"belize_", "bogota_", "falklands_", "las_vegas_"}) {
    INFO(n);
    const Resource* r = t.lookup(n)->resourceOf("Ce_MapMELODICLAYER.ogg");
    REQUIRE(r);
    CHECK(r->primary == "cadi/audio/Ce_MapMELODICLAYER.ogg");
  }
  CHECK(t.lookup("zurich")->resourceOf("footsteps.ogg")->primary == "cadi/audio/footsteps_2.ogg");
  CHECK(t.lookup("market")->resourceOf("footsteps.ogg")->primary == "cadi/audio/footsteps.ogg");
  // No Reforged copy: the path-qualified CAD file first, the basename as a fallback.
  const Resource& bank = t.lookup("bank")->fileRes[0];
  CHECK(bank.primary.rfind("cadi/cad/Resources/", 0) == 0);
  CHECK(bank.primary.find("environments/base/bank.ogg") != std::string::npos);
  CHECK(bank.fallback == "cadi/audio/bank.ogg");
  for (const EventDef& d : t.events()) {
    INFO(d.name);
    if (d.kind == Kind::Silent || d.kind == Kind::Direct) continue;
    CHECK(d.fileRes.size() == d.files.size());
    CHECK(d.sweetenerRes.size() == d.sweeteners.size());
    for (const Resource& r : d.fileRes) CHECK(!r.primary.empty());
  }
}

TEST_CASE("EventTable: typing names are silent; only ui_click and the fireworks are direct") {
  const EventTable& t = choco::games::choc3::audioEvents();
  for (const char* n : {"sfx_typing_01", "sfx_typing_02", "sfx_typing_03", "sfx_typing_04"}) CHECK(t.lookup(n)->kind == Kind::Silent);
  CHECK(t.lookup("ui_click")->kind == Kind::Direct);
  for (const char* n : {"firework1", "firework5"}) CHECK(t.lookup(n)->kind == Kind::Direct);
  for (const EventDef& d : t.events())
    if (d.kind == Kind::Direct) CHECK((d.name == "ui_click" || d.name.rfind("firework", 0) == 0));
}

TEST_CASE("EventTable: no ambience has a zero-width interval") {
  for (const EventDef& d : choco::games::choc3::audioEvents().events()) {
    if (d.kind != Kind::AmbienceLoop || d.sweeteners.empty()) continue;
    INFO(d.name);
    CHECK(d.intervalMaxMs > 0);
    CHECK(d.intervalMaxMs >= d.intervalMinMs);
  }
}

TEST_CASE("EventTable: arrangement timing comes from the clips (AU s.10.5)") {
  const EventTable& t = choco::games::choc3::audioEvents();
  const MusicRoles& lima = t.lookup("lima_")->music;
  CHECK(!lima.loop);
  CHECK(lima.lengthFrames == 1656612);
  CHECK(lima.handoverFrames == 276102);
  CHECK(lima.under == "Ce_MapPERCUSSIVELAYER.ogg");
  CHECK(lima.tail == "portmusicFADE.ogg");
  CHECK(t.lookup("tangiers_")->music.loop);  // morocco_sting loops the whole arrangement
  CHECK(t.lookup("reykjavik_")->music.loop);
  CHECK(t.lookup("main_menu_")->music.lengthFrames == 1612800);
  CHECK(t.lookup("C3 Gameplay 1")->music.lengthFrames == 4208085);
  int nonLoopingPorts = 0;
  for (const EventDef& d : t.events()) {
    if (d.kind != Kind::Music || d.music.sting.empty()) continue;
    CHECK(d.music.handoverFrames > 0);
    nonLoopingPorts += !d.music.loop;
  }
  CHECK(nonLoopingPorts == 18);
}

TEST_CASE("Audio: a port sting is a non-looping clip whose layers enter at the hand-over offset") {
  Harness h;
  h.audio.event("lima_sting");
  const MusicTrack* t = h.audio.mixer().current();
  REQUIRE(t);
  // sting + percussive under it from 0, the layers at 276,102, then portmusicFADE after them.
  REQUIRE(t->stages.size() == 4);
  CHECK(t->stages[0].startFrame == 0);
  CHECK(!t->stages[0].loop);
  CHECK(t->stages[1].startFrame == 0);
  CHECK(t->stages[1].lengthFrames == 276102);
  CHECK(t->stages[2].startFrame == 276102);
  CHECK(t->stages[2].sources.size() == 2);
  CHECK(!t->stages[2].loop);
  CHECK(t->stages[3].startFrame == 276102 + t->stages[2].sources[0].frames());  // right after the layers
  CHECK(t->endFrames == 1656612);
  CHECK(t->loopFrames == 0);
  CHECK(h.audio.dump().find("stage=1/3") != std::string::npos);
  // The exact file for the melodic layer was asked for.
  CHECK(std::count(h.requested.begin(), h.requested.end(), "cadi/audio/Ce_MapMELODICLAYER (2).ogg") == 1);

  Harness m;
  m.audio.event("morocco_sting");
  CHECK(m.audio.mixer().current()->loopFrames == 1748646);
  CHECK(m.audio.mixer().current()->endFrames == 0);
  Harness r;
  r.audio.event("reykjavik_sting");
  REQUIRE(r.audio.mixer().current());
  CHECK(r.audio.mixer().current()->stages.size() == 1);
  CHECK(r.audio.mixer().current()->stages[0].loop);
}

TEST_CASE("Audio: a lima clip plays out and ends at its length; the layers are silent before the hand-over") {
  Harness h;
  h.audio.setLevels(1, 1, 1, false);
  h.audio.event("lima_sting");
  h.audio.update(0);
  // The fixture sting lasts 0.5 s; the layers only enter at 276,102 frames = 6.26 s.
  h.audio.update(4000);
  CHECK(h.audio.mixer().current() != nullptr);
  CHECK(h.audio.mixer().current()->timeline > 150000);
  h.audio.update(60000);  // past the 1,656,612-frame clip (37.6 s)
  CHECK(h.audio.mixer().current() == nullptr);
  CHECK(h.audio.dump().find("audio music none\n") != std::string::npos);
}

TEST_CASE("Audio: a clip fades in over its master fade, else 1000 ms; the arrangement's fade is not used") {
  Harness a;
  a.audio.event("map_");  // the channel name: no clip, so no master fade stated (its arrangement fade is 2210)
  CHECK(a.count("-> music map_ fade 1000/1000") == 1);
  Harness b;
  b.audio.event("chocolate_making_game_outro");  // a clip with no fades at all
  CHECK(b.count("fade 1000/1000") == 1);
  Harness c;
  c.audio.event("map_screen");
  CHECK(c.count("fade 2580/960") == 1);
  // Stop_Music fades over the playing clip's master fade.
  c.audio.event("Stop_Music");
  c.audio.update(0);
  c.audio.update(2000);
  CHECK(c.audio.mixer().fading() != nullptr);
  c.audio.update(2700);
  CHECK(c.audio.mixer().fading() == nullptr);
}

TEST_CASE("Audio: music streams per track and is not decoded into the sound cache") {
  Harness h;
  h.audio.event("lima_sting");
  REQUIRE(h.audio.mixer().current());
  CHECK(h.audio.cache().decodedCount() == 0);
  CHECK(h.audio.mixer().current()->owned.empty());
  CHECK(h.audio.mixer().current()->streams.size() == 5);  // sting, under, both layers, the fade
  h.audio.event("buy");
  CHECK(h.audio.cache().decodedCount() == 1);
}

TEST_CASE("Audio: the primary resource is tried first and the basename is the fallback, without a missing-file log") {
  Harness h(1, [](const std::string& p) { return p.rfind("cadi/audio/", 0) == 0; });  // like Reforged: no cadi/cad/
  h.audio.event("bank");  // no Reforged copy of the vanilla file
  CHECK(h.audio.mixer().activeVoices() == 1);
  CHECK(h.count("missing file") == 0);
  CHECK(h.requested.front().rfind("cadi/cad/", 0) == 0);
  CHECK(std::count(h.requested.begin(), h.requested.end(), "cadi/audio/bank.ogg") == 1);

  Harness v(1, [](const std::string& p) { return p.rfind("cadi/cad/", 0) == 0; });  // vanilla: only the CAD source
  v.audio.event("buy");  // Reforged has the file; the vanilla fallback is the path-qualified one
  CHECK(v.audio.mixer().activeVoices() == 1);
  CHECK(v.count("missing file") == 0);

  Harness none(1, [](const std::string&) { return false; });
  none.audio.event("buy");
  CHECK(none.audio.mixer().activeVoices() == 0);
  CHECK(none.count("missing file") == 1);
}

TEST_CASE("Audio: launch skips a child whose voice is still playing") {
  Harness h;
  for (int i = 0; i < 10; ++i) h.audio.event("launch");
  CHECK(h.audio.mixer().activeVoices() == 10);  // all ten children, each once: a playing child is never picked again
  std::set<std::string> picks;
  for (const std::string& l : h.lines) picks.insert(l);
  CHECK(picks.size() == 10);
  h.audio.event("launch");
  CHECK(h.audio.mixer().activeVoices() == 10);
  CHECK(h.count("every child is still playing") == 1);
  h.audio.update(0);
  h.audio.update(1000);  // the 0.5 s fixtures are over
  h.audio.event("launch");
  CHECK(h.audio.mixer().activeVoices() == 1);
}

TEST_CASE("Audio: sweetener gain is channelVolume x (1 - volvary x u)") {
  Harness h(3);
  h.audio.event("bank");  // volvary 0.305
  const EventDef& d = *h.audio.table().lookup("bank");
  REQUIRE(d.volVary > 0);
  h.audio.update(0);
  int seen = 0;
  for (int ms = 50; ms <= 30000; ms += 50) {
    h.audio.update(ms);
    for (int v = 1; v < kMaxVoices; ++v) {
      const Voice& voice = h.audio.mixer().voice(v);
      if (!voice.active) continue;
      CHECK(voice.gain <= d.channelVolume);
      CHECK(voice.gain >= d.channelVolume * (1.0f - d.volVary) - 1e-6f);
      ++seen;
    }
  }
  CHECK(seen > 0);
}

TEST_CASE("Audio: a full mixer logs the dropped sounds once") {
  Harness h;
  for (int i = 0; i < 70; ++i) h.audio.event("buy");
  CHECK(h.audio.mixer().activeVoices() == kMaxVoices);
  CHECK(h.audio.mixer().dropped() == 6);
  CHECK(h.count("voice cap reached") == 1);
}

// ---- streaming music ----

TEST_CASE("OggStream: reads equal decodeOgg sample for sample, rewind starts over, the end reads 0") {
  const auto bytes = readAll(kFixtures / "sine_stereo.ogg");
  const auto pcm = decodeOgg(bytes);
  REQUIRE(pcm.has_value());
  auto s = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(s);
  CHECK(s->channels() == 2);
  CHECK(s->totalFrames() == pcm->frames());
  std::vector<int16_t> got;
  std::vector<int16_t> chunk(300 * 2);  // an odd chunk size, to cross stb_vorbis frame boundaries
  for (std::size_t n; (n = s->read(chunk.data(), 300)) > 0;) got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
  CHECK(got == pcm->samples);
  CHECK(s->read(chunk.data(), 300) == 0);
  REQUIRE(s->rewind());
  got.clear();
  for (std::size_t n; (n = s->read(chunk.data(), 300)) > 0;) got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
  CHECK(got == pcm->samples);
  // nextFrame agrees too, and a rewound stream starts at frame 0 again.
  REQUIRE(s->rewind());
  for (std::size_t i = 0; i < pcm->frames(); ++i) {
    int16_t l = 0, r = 0;
    REQUIRE(s->nextFrame(l, r));
    if (l != pcm->samples[i * 2] || r != pcm->samples[i * 2 + 1]) FAIL("frame " << i << " differs");
  }
  int16_t l, r;
  CHECK(!s->nextFrame(l, r));
  // Silent mode keeps the length and decodes nothing.
  auto q = OggStream::open(bytes.data(), bytes.size());
  q->setSilent(true);
  std::size_t n = 0;
  while (q->nextFrame(l, r)) ++n;
  CHECK(n == pcm->frames());
  const std::vector<uint8_t> junk(100, 7);
  CHECK(!OggStream::open(junk.data(), junk.size()));
}

TEST_CASE("OggStream: a 22.05 kHz file is resampled to 44100 by linear interpolation") {
  const auto bytes = readAll(kFixtures / "sine_22k.ogg");
  auto raw = OggStream::open(bytes.data(), bytes.size());
  auto s = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(raw);
  REQUIRE(s);
  std::vector<int16_t> src;
  std::vector<int16_t> chunk(300 * 2);
  for (std::size_t n; (n = raw->read(chunk.data(), 300)) > 0;) src.insert(src.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
  const std::size_t frames = src.size() / 2;
  REQUIRE(frames > 100);
  CHECK(s->totalFrames() == frames * 2);  // twice the frames at twice the rate
  std::vector<int16_t> out;
  int16_t l = 0, r = 0;
  while (s->nextFrame(l, r)) out.insert(out.end(), {l, r});
  REQUIRE(out.size() == frames * 4);
  for (std::size_t k = 0; k + 1 < frames; ++k) {
    // Even output frames are the source frames, odd ones the midpoint of two neighbours (truncated).
    REQUIRE(out[k * 4] == src[k * 2]);
    REQUIRE(out[k * 4 + 1] == src[k * 2 + 1]);
    REQUIRE(std::abs(out[k * 4 + 2] - (src[k * 2] + src[k * 2 + 2]) / 2) <= 1);
  }
  // A rewound stream starts over from the same first frames.
  REQUIRE(s->rewind());
  REQUIRE(s->nextFrame(l, r));
  CHECK(l == src[0]);
  CHECK(r == src[1]);
  // Silent mode counts the resampled length.
  auto q = OggStream::open(bytes.data(), bytes.size());
  q->setSilent(true);
  std::size_t n = 0;
  while (q->nextFrame(l, r)) ++n;
  CHECK(n == frames * 2);
}

TEST_CASE("Mixer: a streamed looping stage matches the same Pcm and wraps at the file's end") {
  const auto bytes = readAll(kFixtures / "sine_stereo.ogg");
  const auto pcm = decodeOgg(bytes);
  REQUIRE(pcm.has_value());
  auto stream = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(stream);
  Mixer a, b;
  MusicTrack ta, tb;
  MusicStage sa, sb;
  sa.sources.push_back(&*pcm);
  sb.sources.push_back(stream.get());
  sa.loop = sb.loop = true;
  ta.stages.push_back(sa);
  tb.stages.push_back(sb);
  REQUIRE(a.startTrack(&ta, 0));
  REQUIRE(b.startTrack(&tb, 0));
  const std::size_t frames = pcm->frames() * 2 + 123;  // past one wrap
  std::vector<float> oa(frames * 2), ob(frames * 2);
  a.render(oa.data(), frames);
  b.render(ob.data(), frames);
  CHECK(oa == ob);
}

TEST_CASE("Mixer: a stage rewinds its stream when it starts, so successive clips of one stem restart it") {
  const auto bytes = readAll(kFixtures / "sine_stereo.ogg");
  const auto pcm = decodeOgg(bytes);
  auto stream = OggStream::open(bytes.data(), bytes.size());
  Mixer m;
  MusicTrack t;
  for (uint64_t start : {0u, 1000u}) {  // two 100-frame clips of one stream
    MusicStage st;
    st.sources.push_back(stream.get());
    st.startFrame = start;
    st.lengthFrames = 100;
    t.stages.push_back(st);
  }
  t.loopFrames = 2000;
  REQUIRE(m.startTrack(&t, 0));
  std::vector<float> out(2200 * 2);
  m.render(out.data(), 2200);
  for (int i : {0, 99, 1000, 1099, 2000, 2050}) {
    const int k = i % 1000;
    CHECK(out[static_cast<std::size_t>(i) * 2] == doctest::Approx(static_cast<float>(pcm->samples[static_cast<std::size_t>(k) * 2]) / 32768.0f));
  }
  CHECK(out[100 * 2] == 0.0f);  // between the clips
  CHECK(out[1500 * 2] == 0.0f);
}

TEST_CASE("Mixer: crossfade sums the outgoing and incoming ramps mid-fade, each over its own time") {
  const Pcm one = constant(16384, 1000);
  Mixer m;
  MusicTrack a, b;
  a.stages.push_back({{&one}, true});
  b.stages.push_back({{&one}, true});
  REQUIRE(m.startTrack(&a, 0, 0));
  std::vector<float> skip(10 * 2);
  m.render(skip.data(), 10);
  REQUIRE(m.startTrack(&b, 2, 1));  // in over 2 ms = 88 frames, out over 1 ms = 44 frames
  const std::vector<float> l = renderLeft(m, 100);
  // frame f: incoming f/88, outgoing max(0, 1 - f/44), each of the two tracks 0.5 at full gain
  CHECK(l[0] == doctest::Approx(0.5f));
  CHECK(l[22] == doctest::Approx(0.5f * (22.0f / 88.0f + 0.5f)));
  CHECK(l[44] == doctest::Approx(0.5f * (44.0f / 88.0f)));
  CHECK(l[66] == doctest::Approx(0.5f * (66.0f / 88.0f)));
  CHECK(l[90] == doctest::Approx(0.5f));
}

namespace {

// All blocks of one stem of a port_music cue, by stem file.
std::vector<const MusicClip*> clipsOf(const EventDef& d, const std::string& file) {
  std::vector<const MusicClip*> out;
  for (const MusicClip& c : d.music.clips)
    if (c.file == file) out.push_back(&c);
  return out;
}

}  // namespace

TEST_CASE("EventTable: port_music cues carry the per-stem clip timelines of audio-port-music.tsv") {
  const EventTable& t = choco::games::choc3::audioEvents();
  std::size_t total = 0;
  for (int n = 1; n <= 3; ++n) {
    const EventDef* d = t.lookup("port_music" + std::to_string(n));
    REQUIRE(d);
    CHECK(d->music.loop);
    CHECK(!d->music.clips.empty());
    total += d->music.clips.size();
    for (const MusicClip& c : d->music.clips) {
      CHECK(c.lengthFrames > 0);
      CHECK(c.startFrame + c.lengthFrames <= d->music.lengthFrames);
    }
  }
  CHECK(total == 66);
  const EventDef& c1 = *t.lookup("port_music1");
  CHECK(c1.music.lengthFrames == 4208085);
  const auto bass = clipsOf(c1, "C3G1_Bass.ogg");
  REQUIRE(bass.size() == 5);
  CHECK(bass[4]->startFrame == 3356192);
  CHECK(bass[4]->lengthFrames == 839048);  // the 841,622 window is cut to the resource's loop region
  CHECK(bass[0]->gain == doctest::Approx(0.97622f));
  const auto drums = clipsOf(c1, "C3G1_Drums.ogg");
  REQUIRE(drums.size() == 4);
  CHECK(drums[1]->gain == doctest::Approx(0.082549f * 0.944783f));  // clip volume x track volume
  CHECK(clipsOf(c1, "C3G1_Sax.ogg").size() == 1);
  CHECK(clipsOf(*t.lookup("port_music2"), "C3G2_HornLine.ogg").size() == 1);
}

TEST_CASE("Audio: port_music<n> builds one stage per stem clip and loops at the cue length") {
  Harness h;
  h.audio.event("port_music1");
  const MusicTrack* t = h.audio.mixer().current();
  REQUIRE(t);
  CHECK(t->stages.size() == 20);  // the clips of the eight stems
  CHECK(t->streams.size() == 8);  // one stream per stem, shared by its clips
  CHECK(t->loopFrames == 4208085);
  CHECK(t->endFrames == 0);
  CHECK(h.audio.dump().find("audio music C3 Gameplay 1 stage=1/5\n") != std::string::npos);
  CHECK(h.count("port_music1 -> music C3 Gameplay 1 (alias) fade 3400/1600") == 1);
  h.audio.event("port_music2");
  CHECK(h.audio.mixer().current()->streams.size() == 9);
  CHECK(h.audio.mixer().fading() == t);
}

TEST_CASE("Audio: Stop_Music then port_music<n>, and the same event twice does not rebuild") {
  Harness h;
  h.audio.event("lima_sting");
  h.audio.event("Stop_Music");
  h.audio.event("port_music3");
  const MusicTrack* t = h.audio.mixer().current();
  REQUIRE(t);
  h.audio.event("port_music3");
  CHECK(h.audio.mixer().current() == t);
  CHECK(h.count("port_music3 -> music C3 Gameplay 3 ignored") == 1);
}

TEST_CASE("Audio: a cue loops without end and its timeline wraps at the cue length") {
  Harness h;
  h.audio.setLevels(1, 1, 1, false);
  h.audio.event("port_music2");
  h.audio.update(0);
  h.audio.update(80000);  // 80 s of audio time: past the 67.8 s cue length
  const MusicTrack* t = h.audio.mixer().current();
  REQUIRE(t);
  CHECK(t->timeline < 2989280);
  CHECK(!t->done);
}

TEST_CASE("Audio: skip-decode pump keeps track timing and the dump, and renders silence") {
  Harness a, b;
  b.audio.setSkipDecode(true);
  for (Harness* h : {&a, &b}) {
    h->audio.setLevels(1, 1, 1, false);
    h->audio.event("port_music2");
    h->audio.update(0);
    for (int ms = 1000; ms <= 80000; ms += 1000) h->audio.update(ms);  // past the 67.8 s cue wrap
  }
  const MusicTrack* ta = a.audio.mixer().current();
  const MusicTrack* tb = b.audio.mixer().current();
  REQUIRE(ta);
  REQUIRE(tb);
  CHECK(ta->timeline == tb->timeline);
  CHECK(ta->timeline > 0);
  CHECK(a.audio.dump() == b.audio.dump());
  CHECK(a.audio.mixer().framesRendered() == b.audio.mixer().framesRendered());
  // The fixtures are 0.5 s long, so compare the first block of a fresh cue: audible when decoded, all zeros when skipped.
  Harness c, d;
  d.audio.setSkipDecode(true);
  std::vector<float> outC(4096 * 2), outD(4096 * 2);
  for (Harness* h : {&c, &d}) {
    h->audio.setLevels(1, 1, 1, false);
    h->audio.event("port_music2");
  }
  c.audio.mixer().render(outC.data(), 4096);
  d.audio.mixer().render(outD.data(), 4096);
  CHECK(std::any_of(outC.begin(), outC.end(), [](float v) { return v != 0.0f; }));
  CHECK(std::all_of(outD.begin(), outD.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("OggStream: a stream whose reads give nothing after a rewind is dead and a loop stops retrying") {
  const auto bytes = readAll(kFixtures / "sine_stereo.ogg");
  auto s = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(s);
  s->forceEmptyReadsForTest();
  int16_t l, r;
  CHECK(!s->nextFrame(l, r));
  CHECK(s->dead());
  CHECK(!s->rewind());  // dead for good: no more seeks
  CHECK(!s->nextFrame(l, r));
  // A looping stage over it renders silence and does not wedge.
  Mixer m;
  MusicTrack t;
  MusicStage st;
  st.sources.push_back(s.get());
  st.loop = true;
  t.stages.push_back(st);
  // Source::valid() needs frames(), which is the file length, so the track still starts.
  REQUIRE(m.startTrack(&t, 0));
  std::vector<float> out(2000 * 2);
  m.render(out.data(), 2000);
  CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("Mixer: stopTrack reports failure when the retired slots are full and changes nothing") {
  const Pcm p = constant(100, 10);
  Mixer m;
  MusicTrack t;
  t.stages.push_back({{&p}, true});
  CHECK(!m.stopTrack(0));  // no current track
  REQUIRE(m.startTrack(&t, 0));
  CHECK(m.stopTrack(0));
  CHECK(m.current() == nullptr);
}

TEST_CASE("OggStream: a real Reforged stem streams identically to decodeOgg") {
  const fs::path file = fs::path(CHOCO_REFORGED_ASSETS) / "cadi" / "audio" / "C3G1_Sax.ogg";
  if (!fs::exists(file)) {
    MESSAGE("SKIPPED: " << file.string() << " not present");
    return;
  }
  const auto bytes = readAll(file);
  const auto pcm = decodeOgg(bytes);
  REQUIRE(pcm.has_value());
  auto s = OggStream::open(bytes.data(), bytes.size());
  REQUIRE(s);
  CHECK(s->totalFrames() == pcm->frames());
  std::vector<int16_t> got(pcm->samples.size() + 8192);
  std::size_t at = 0;
  const std::size_t ch = static_cast<std::size_t>(s->channels());
  for (std::size_t n; (n = s->read(got.data() + at, 4096)) > 0;) at += n * ch;
  got.resize(at);
  CHECK(got == pcm->samples);
}

// ---- prefetch worker ----

namespace {

// A loader that counts reads per path (it runs on the worker too) and can hold one path until released.
struct CountingLoader {
  std::mutex m;
  std::condition_variable cv;
  std::map<std::string, int> loads;
  std::string hold;  // a read of this path blocks until `release`
  bool entered = false, released = true;

  std::optional<SoundCache::Bytes> operator()(const std::string& path) {
    std::unique_lock<std::mutex> lock(m);
    ++loads[path];
    if (path == hold) {
      entered = true;
      cv.notify_all();
      cv.wait(lock, [&] { return released; });
    }
    lock.unlock();
    if (path == "good.ogg" || path == "twin.ogg" || path == "other.ogg") return readAll(kFixtures / "sine_stereo.ogg");
    if (path == "bad.ogg") return SoundCache::Bytes(100, 7);
    return std::nullopt;
  }
  int count(const std::string& p) {
    std::lock_guard<std::mutex> lock(m);
    return loads[p];
  }
  void waitEntered() {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return entered; });
  }
  void release() {
    {
      std::lock_guard<std::mutex> lock(m);
      released = true;
    }
    cv.notify_all();
  }
};

}  // namespace

TEST_CASE("SoundCache prefetch: the worker's decode equals a synchronous one, failures log once, nothing successful decodes twice") {
  CountingLoader loader;
  loader.hold = "good.ogg";  // the worker is inside good.ogg's read, so the result below is the worker's
  loader.released = false;
  std::vector<std::string> lines;
  SoundCache cache([&](const std::string& p) { return loader(p); }, [&](const std::string& l) { lines.push_back(l); });
  cache.startPrefetch({{"good.ogg", {}}, {"bad.ogg", {}}, {"missing.ogg", {}}, {"nope.ogg", "twin.ogg"}});
  loader.waitEntered();
  std::thread releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loader.release();
  });
  const Pcm* viaWorker = cache.pcm("good.ogg");
  releaser.join();
  REQUIRE(viaWorker);
  const Pcm* viaFallback = cache.pcm("nope.ogg", "twin.ogg");
  REQUIRE(viaFallback);
  CHECK(!cache.pcm("bad.ogg"));
  CHECK(!cache.pcm("missing.ogg"));
  CHECK(!cache.pcm("missing.ogg"));

  SoundCache plain([&](const std::string& p) { return loader(p); }, nullptr);
  const Pcm* sync = plain.pcm("good.ogg");
  REQUIRE(sync);
  CHECK(viaWorker->channels == sync->channels);
  CHECK(viaWorker->samples == sync->samples);
  CHECK(viaFallback->samples == sync->samples);

  CHECK(cache.pcm("good.ogg") == viaWorker);  // kept, not decoded again
  // The worker read good.ogg once; the plain cache above read it a second time, nothing else did.
  CHECK(loader.count("good.ogg") == 2);
  // The failures are logged by pcm() on the main thread, once each.
  int rejected = 0, missing = 0;
  for (const std::string& l : lines) {
    rejected += l.find("rejected bad.ogg") != std::string::npos;
    missing += l.find("missing file missing.ogg") != std::string::npos;
  }
  CHECK(rejected == 1);
  CHECK(missing == 1);
}

TEST_CASE("SoundCache prefetch: pcm() of a running decode waits for it, one that has not started is taken by pcm()") {
  CountingLoader loader;
  loader.hold = "good.ogg";
  loader.released = false;
  SoundCache cache([&](const std::string& p) { return loader(p); }, nullptr);
  cache.startPrefetch({{"good.ogg", {}}, {"other.ogg", {}}});
  loader.waitEntered();  // the worker is inside good.ogg's read, other.ogg is still queued
  const Pcm* other = cache.pcm("other.ogg");
  REQUIRE(other);
  CHECK(loader.count("other.ogg") == 1);

  std::thread releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loader.release();
  });
  const Pcm* good = cache.pcm("good.ogg");  // blocks until the worker finishes it
  releaser.join();
  REQUIRE(good);
  CHECK(good->samples == other->samples);
  CHECK(loader.count("good.ogg") == 1);
  CHECK(loader.count("other.ogg") == 1);  // the worker skipped it
}

TEST_CASE("SoundCache prefetch: one summary line when the worker is done") {
  CountingLoader loader;
  std::vector<std::string> lines;
  SoundCache cache([&](const std::string& p) { return loader(p); }, [&](const std::string& l) { lines.push_back(l); });
  cache.startPrefetch({{"good.ogg", {}}, {"twin.ogg", {}}});
  cache.pcm("twin.ogg");
  cache.pcm("good.ogg");
  for (int i = 0; i < 2000 && lines.empty(); ++i) {
    cache.pollPrefetch();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  cache.pollPrefetch();
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].rfind("[audio] prefetched ", 0) == 0);
  CHECK(lines[0].find(" sounds, ") != std::string::npos);
}

TEST_CASE("Audio::prefetchList: derived from the event table, no music, includes the direct clicks") {
  const auto list = Audio::prefetchList(choco::games::choc3::audioEvents());
  auto has = [&](const std::string& primary) {
    return std::any_of(list.begin(), list.end(), [&](const auto& r) { return r.primary == primary; });
  };
  CHECK(has("cadi/audio/travel_plane.ogg"));
  CHECK(has("cadi/ui_click.ogg"));
  CHECK(has("sfx/main_rollover.ogg"));
  CHECK(has("sfx/sfx_typing_01.ogg"));
  CHECK(has("sfx/firework3.ogg"));
  // Every sweetener of every ambience is listed, and no music stem is.
  for (const EventDef& d : choco::games::choc3::audioEvents().events()) {
    if (d.kind == Kind::Music) {
      for (const std::string& f : d.files) CHECK(!has("cadi/audio/" + f));
    }
    if (d.kind == Kind::AmbienceLoop) {
      for (std::size_t i = 0; i < d.sweetenerRes.size(); ++i) CHECK(has(d.sweetenerRes[i].primary));
    }
  }
}

// ---- Chocolatier 2: file-driven music and ambience (docs/re/choc2-audio.md) ----
// sine_stereo.ogg is half a second long, so every "file" below is a 0.5 s clip and a fake clock steps over it.

namespace {

bool hasLine(const std::string& dump, const std::string& line) { return dump.find(line + "\n") != std::string::npos; }
bool hasPrefix(const std::string& dump, const std::string& prefix) { return dump.find(prefix) != std::string::npos; }

}  // namespace

TEST_CASE("Audio C2: playMusicFile streams once or loops, stopMusic fades, and the dump names the file") {
  Harness h;
  h.audio.update(0);
  CHECK(h.audio.playMusicFile("music/a.ogg", false, 0, 1000));
  CHECK(hasPrefix(h.audio.dump(), "audio music music/a.ogg stage=1/1\n"));
  h.audio.update(700);  // a 0.5 s one-shot has ended and is reaped
  CHECK(hasLine(h.audio.dump(), "audio music none"));
  CHECK(h.audio.playMusicFile("music/b.ogg", true, 0, 1000));
  h.audio.update(5000);  // a loop outlives its clip
  CHECK(hasPrefix(h.audio.dump(), "audio music music/b.ogg"));
  h.audio.stopMusic(500);
  CHECK(hasLine(h.audio.dump(), "audio music none"));
  h.audio.update(6000);
  CHECK(h.audio.mixer().current() == nullptr);
  CHECK(h.audio.mixer().fading() == nullptr);
}

TEST_CASE("Audio C2: a missing music file logs once and is silent") {
  Harness h(1, [](const std::string& p) { return p != "music/gone.ogg"; });
  h.audio.update(0);
  CHECK(h.audio.playMusicFile("music/a.ogg", true, 0, 1000));
  CHECK_FALSE(h.audio.playMusicFile("music/gone.ogg", true, 0, 1000));
  CHECK_FALSE(h.audio.playMusicFile("music/gone.ogg", true, 0, 1000));
  CHECK(h.count("missing file music/gone.ogg") == 1);
  CHECK(hasLine(h.audio.dump(), "audio music none"));
}

TEST_CASE("Audio C2: pausing the music freezes its position and leaves the ambience and the levels alone") {
  Harness h;
  h.audio.update(0);
  h.audio.setAmbientFile("sfx/port_loop.ogg");
  h.audio.playMusicFile("music/b.ogg", true, 0, 1000);
  h.audio.update(100);
  h.audio.setMusicPaused(true);
  CHECK(hasPrefix(h.audio.dump(), "audio music music/b.ogg stage=1/1 paused\n"));
  CHECK(hasLine(h.audio.dump(), "audio env sfx/port_loop.ogg"));
  CHECK(h.audio.dump().find("pause=none") != std::string::npos);
  const uint64_t at = h.audio.mixer().current()->timeline;
  h.audio.update(900);
  CHECK(h.audio.mixer().current()->timeline == at);  // the track did not advance, the clock did
  CHECK(h.audio.mixer().hasTag(-2));                 // the bed still plays
  h.audio.setMusicPaused(false);
  h.audio.update(1000);
  CHECK(h.audio.mixer().current()->timeline > at);
  CHECK(hasPrefix(h.audio.dump(), "audio music music/b.ogg stage=1/1\n"));
}

TEST_CASE("Audio C2: SetAmbient replaces with a crossfade, repeats are left alone, \"\" stops, a missing bed is silent") {
  Harness h(1, [](const std::string& p) { return p != "sfx/market_loop.ogg"; });
  h.audio.update(0);
  h.audio.setAmbientFile("sfx/port_loop.ogg");
  CHECK(hasLine(h.audio.dump(), "audio env sfx/port_loop.ogg"));
  h.audio.update(100);
  h.audio.setAmbientFile("sfx/port_loop.ogg");
  CHECK(h.count("ambient sfx/port_loop.ogg ignored") == 1);
  CHECK(h.audio.mixer().activeVoices() == 1);
  h.audio.setAmbientFile("sfx/farm_loop.ogg");
  CHECK(hasLine(h.audio.dump(), "audio env sfx/farm_loop.ogg"));
  CHECK(h.audio.mixer().activeVoices() == 2);  // the old bed is fading out over 300 ms
  h.audio.update(600);
  CHECK(h.audio.mixer().activeVoices() == 1);
  h.audio.setAmbientFile("sfx/market_loop.ogg");  // the missing bed of ports/paris.lua
  CHECK(hasLine(h.audio.dump(), "audio env none"));
  h.audio.setAmbientFile("sfx/market_loop.ogg");
  CHECK(h.count("missing file sfx/market_loop.ogg") == 1);
  h.audio.setAmbientFile("sfx/farm_loop.ogg");
  h.audio.setAmbientFile("");
  CHECK(hasLine(h.audio.dump(), "audio env none"));
  h.audio.update(1200);
  CHECK(h.audio.mixer().activeVoices() == 0);
}
