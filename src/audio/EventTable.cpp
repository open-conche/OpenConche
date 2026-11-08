#include "audio/EventTable.h"

#include <stdexcept>
#include <utility>

namespace choco::audio {

namespace {

std::vector<std::string> split(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* p = text; *p; ++p) {
    if (*p == ';') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += *p;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::vector<Resource> splitResources(const char* text) {
  std::vector<Resource> out;
  for (const std::string& item : split(text)) {
    const auto bar = item.find('|');
    out.push_back({item.substr(0, bar), bar == std::string::npos ? std::string() : item.substr(bar + 1)});
  }
  return out;
}

}  // namespace

EventTable::EventTable(const EventTableData& data) {
  events_.reserve(data.events.size());
  for (const StaticEvent& s : data.events) {
    EventDef d;
    d.name = s.name;
    d.kind = s.kind;
    d.group = s.group;
    d.channelVolume = s.channelVolume;
    d.files = split(s.files);
    d.sweeteners = split(s.sweeteners);
    d.fileRes = splitResources(s.fileRes);
    d.sweetenerRes = splitResources(s.sweetenerRes);
    if (d.fileRes.size() != d.files.size() && !d.fileRes.empty()) throw std::logic_error("event table: fileRes of " + d.name);
    d.intervalMinMs = s.intervalMin;
    d.intervalMaxMs = s.intervalMax;
    d.attackMs = s.attack;
    d.releaseMs = s.release;
    d.volVary = s.volVary;
    d.fadeMs = s.fade;
    d.srcFadeMs = s.srcFade;
    byName_.emplace(d.name, static_cast<int>(events_.size()));
    events_.push_back(std::move(d));
  }
  for (std::size_t i = 0; i < events_.size(); ++i) {
    EventDef& d = events_[i];
    const std::string stop = data.events[i].stop;
    if (stop == "@Music") {
      d.stopGroup = StopGroup::Music;
    } else if (stop == "@Environments") {
      d.stopGroup = StopGroup::Environments;
    } else if (!stop.empty()) {
      auto it = byName_.find(stop);
      if (it == byName_.end()) throw std::logic_error("event table: stop target " + stop);
      d.stopTarget = it->second;
    }
  }
  for (const StaticMusic& m : data.music) {
    auto it = byName_.find(m.arrangement);
    if (it == byName_.end()) throw std::logic_error(std::string("event table: arrangement ") + m.arrangement);
    MusicRoles& r = events_[static_cast<std::size_t>(it->second)].music;
    r.present = true;
    r.status = m.status;
    r.sting = m.sting;
    r.layers = split(m.layers);
    r.intro = m.intro;
    r.body = m.body;
    r.ending = m.ending;
    r.under = m.under;
    r.tail = m.tail;
    r.loop = m.loop;
    r.lengthFrames = m.length;
    r.handoverFrames = m.handover;
  }
  for (const StaticClip& c : data.clips) {
    auto it = byName_.find(c.arrangement);
    if (it == byName_.end()) throw std::logic_error(std::string("event table: clip arrangement ") + c.arrangement);
    events_[static_cast<std::size_t>(it->second)].music.clips.push_back({c.file, c.start, c.length, c.gain});
  }
  for (const StaticAlias& a : data.aliases) {
    aliasByName_.emplace(a.event, aliases_.size());
    aliases_.push_back({a.event, a.arrangement, a.part, a.masterFade, a.sourceFade});
  }
}

EventTable::EventTable(std::vector<EventDef> events) : events_(std::move(events)) {
  for (std::size_t i = 0; i < events_.size(); ++i) byName_.emplace(events_[i].name, static_cast<int>(i));
}

const Resource* EventDef::resourceOf(const std::string& basename) const {
  for (std::size_t i = 0; i < files.size() && i < fileRes.size(); ++i)
    if (files[i] == basename) return &fileRes[i];
  for (std::size_t i = 0; i < sweeteners.size() && i < sweetenerRes.size(); ++i)
    if (sweeteners[i] == basename) return &sweetenerRes[i];
  return nullptr;
}

const EventTable& EventTable::empty() {
  static const EventTable table{EventTableData{}};
  return table;
}

Resolved EventTable::resolve(std::string_view name) const {
  Resolved r;
  if (auto it = byName_.find(name); it != byName_.end()) {
    r.index = it->second;
    r.def = &events_[static_cast<std::size_t>(it->second)];
    return r;
  }
  if (auto it = aliasByName_.find(name); it != aliasByName_.end()) {
    const Alias& a = aliases_[it->second];
    if (auto t = byName_.find(a.arrangement); t != byName_.end()) {
      r.index = t->second;
      r.def = &events_[static_cast<std::size_t>(t->second)];
      r.part = a.part;
      r.alias = true;
      r.masterFadeMs = a.masterFadeMs;
      r.sourceFadeMs = a.sourceFadeMs;
    }
  }
  return r;
}

const EventDef* EventTable::arrangement(std::string_view name) const {
  auto it = byName_.find(name);
  if (it == byName_.end()) return nullptr;
  const EventDef& d = events_[static_cast<std::size_t>(it->second)];
  return d.kind == Kind::Music ? &d : nullptr;
}

}  // namespace choco::audio
