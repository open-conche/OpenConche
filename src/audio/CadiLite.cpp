#include "audio/CadiLite.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace choco::audio {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::vector<std::string> tokens(std::string_view line) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == ' ' || c == '\t' || c == ',') {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

// The text of a `"name"` token: both quotes present, nothing after the closing one.
bool quoted(std::string_view s, std::string* name) {
  if (s.size() < 3 || s.front() != '"' || s.back() != '"') return false;
  *name = std::string(s.substr(1, s.size() - 2));
  return name->find('"') == std::string::npos;
}

std::string at(int line, const std::string& text) { return "[audio] cadilite.spec:" + std::to_string(line) + ": " + text; }

constexpr const char* kSfx = "Sfx";
constexpr const char* kMusic = "Music";
constexpr const char* kEnv = "Environments";

}  // namespace

CadiLiteSpec parseCadiLite(std::string_view text) {
  CadiLiteSpec spec;
  std::set<std::string> defined;  // names of events already closed
  CadiLiteBlock cur;
  bool open = false;
  bool curDuplicate = false;

  auto fail = [&](int line, const std::string& msg) {
    ++spec.errors;
    spec.messages.push_back(at(line, msg));
  };
  auto close = [&]() {
    if (!open) return;
    open = false;
    if (curDuplicate) {
      ++spec.duplicates;
      spec.messages.push_back(at(cur.line, "event \"" + cur.name + "\" is already defined: dropped"));
    } else {
      defined.insert(cur.name);
      spec.blocks.push_back(std::move(cur));
    }
    cur = {};
  };

  int lineNo = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    ++lineNo;
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    if (raw.empty()) {
      close();
      continue;
    }
    if (raw.front() == ':') {
      std::string name;
      if (open) {
        fail(lineNo, "event header inside event \"" + cur.name + "\" (no blank line before it)");
      } else if (!quoted(raw.substr(1), &name)) {
        fail(lineNo, "malformed event header");
      } else {
        open = true;
        cur = {};
        cur.name = name;
        cur.line = lineNo;
        curDuplicate = defined.count(name) != 0;
      }
      continue;
    }
    if (!open) {
      fail(lineNo, "line outside an event");
      continue;
    }
    CadiLiteEntry e;
    e.line = lineNo;
    if (raw.front() == '"') {
      if (!quoted(raw, &e.name)) {
        fail(lineNo, "malformed call");
        continue;
      }
      if (!defined.count(e.name)) {
        fail(lineNo, "call to \"" + e.name + "\", which is not defined on an earlier line: dropped");
        continue;
      }
      e.type = CadiLiteEntry::Type::Call;
      cur.entries.push_back(std::move(e));
      continue;
    }
    const std::vector<std::string> tok = tokens(raw);
    if (!tok.empty() && tok[0] == "play") {
      if (tok.size() < 3 || tok.size() > 4 || (tok.size() == 4 && lower(tok[3]) != "loop")) {
        fail(lineNo, "bad play entry");
        continue;
      }
      e.type = CadiLiteEntry::Type::Play;
      e.group = tok[1];
      e.file = tok[2];
      e.loop = tok.size() == 4;
    } else if (!tok.empty() && tok[0] == "stop") {
      if (tok.size() != 2) {
        fail(lineNo, "bad stop entry");
        continue;
      }
      e.type = CadiLiteEntry::Type::Stop;
      e.file = tok[1];
    } else {
      fail(lineNo, "unknown directive");
      continue;
    }
    cur.entries.push_back(std::move(e));
  }
  close();
  return spec;
}

namespace {

struct Flat {
  std::vector<const CadiLiteEntry*> stops, plays;
};

void flatten(const CadiLiteSpec& spec, const std::map<std::string, std::size_t>& byName, const CadiLiteBlock& b, Flat& out) {
  for (const CadiLiteEntry& e : b.entries) {
    switch (e.type) {
      case CadiLiteEntry::Type::Call:
        flatten(spec, byName, spec.blocks[byName.at(e.name)], out);
        break;
      case CadiLiteEntry::Type::Play: out.plays.push_back(&e); break;
      case CadiLiteEntry::Type::Stop: out.stops.push_back(&e); break;
    }
  }
}

}  // namespace

CadiLiteCompiled compileCadiLite(std::string_view text) {
  const CadiLiteSpec spec = parseCadiLite(text);
  CadiLiteCompiled out;
  out.messages = spec.messages;
  out.errors = spec.errors;
  out.duplicates = spec.duplicates;

  std::map<std::string, std::size_t> byName;
  for (std::size_t i = 0; i < spec.blocks.size(); ++i) byName.emplace(spec.blocks[i].name, i);

  // The files each group plays anywhere in the spec: what a `stop` line can name.
  std::map<std::string, std::set<std::string>> groupFiles;
  for (const CadiLiteBlock& b : spec.blocks)
    for (const CadiLiteEntry& e : b.entries)
      if (e.type == CadiLiteEntry::Type::Play) groupFiles[e.group].insert(e.file);

  auto covers = [&](const std::vector<const CadiLiteEntry*>& stops, const char* group) {
    const auto& files = groupFiles[group];
    if (files.empty()) return false;
    std::set<std::string> stopped;
    for (const CadiLiteEntry* s : stops) stopped.insert(s->file);
    return std::all_of(files.begin(), files.end(), [&](const std::string& f) { return stopped.count(f) != 0; });
  };

  for (const CadiLiteBlock& b : spec.blocks) {
    Flat flat;
    flatten(spec, byName, b, flat);
    EventDef d;
    d.name = b.name;
    d.channelVolume = 1.0f;
    auto setFile = [&](const CadiLiteEntry& p) {
      d.files = {p.file};
      d.fileRes = {{"cadi/" + p.file, {}}};
    };
    bool known = true;
    if (flat.plays.size() == 1) {
      const CadiLiteEntry& p = *flat.plays[0];
      setFile(p);
      if (p.group == kSfx) {
        d.group = Group::Sfx;
        if (flat.stops.empty()) {
          d.kind = p.loop ? Kind::Loop : Kind::Oneshot;
        } else if (!p.loop && flat.stops.size() == 1 && flat.stops[0]->file == p.file) {
          d.kind = Kind::Oneshot;
          d.restart = true;
        } else {
          known = false;
        }
      } else if (p.group == kMusic) {
        // The stops cut the playing music first; the engine's MusicFile cuts on every fire, whatever the stops name.
        d.kind = Kind::MusicFile;
        d.group = Group::Music;
        d.loop = p.loop;
        d.fadeMs = 0;
        d.srcFadeMs = 0;
      } else if (p.group == kEnv) {
        if (p.loop) {
          d.kind = Kind::AmbienceLoop;
          d.group = Group::Env;
        } else {
          known = false;
        }
      } else {
        d.kind = Kind::Silent;  // a group whose level is never set: inaudible
      }
    } else if (flat.plays.empty() && !flat.stops.empty()) {
      if (covers(flat.stops, kEnv)) {
        d.kind = Kind::Stop;
        d.group = Group::Env;
        d.stopGroup = StopGroup::Environments;
      } else if (covers(flat.stops, kMusic)) {
        d.kind = Kind::Stop;
        d.group = Group::Music;
        d.stopGroup = StopGroup::Music;
      } else {
        known = false;
      }
    } else {
      known = false;
    }
    if (!known) {
      d = EventDef{};
      d.name = b.name;
      d.kind = Kind::Unknown;
      ++out.unknown;
      out.messages.push_back(at(b.line, "event \"" + b.name + "\" has no engine equivalent: it does nothing"));
    }
    out.events.push_back(std::move(d));
  }
  return out;
}

}  // namespace choco::audio
