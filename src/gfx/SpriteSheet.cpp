#include "gfx/SpriteSheet.h"

#include "assets/Xml.h"

namespace choco::gfx {

std::string SpriteSheet::defaultScript(std::size_t n) {
  std::string s = "function DoAnim()\n    delay(60);\n    frames{";
  for (std::size_t i = 1; i <= n; ++i) {
    if (i > 1) s += ',';
    s += std::to_string(i);
  }
  return s + "}\nend\n";
}

bool SpriteSheet::parse(std::string_view text, SheetData& out, std::string& error) {
  assets::Xml xml;
  if (!xml.parse(text)) {
    error = "cannot parse XML: " + xml.error();
    return false;
  }
  pugi::xml_node seq = xml.root();
  if (std::string_view(seq.name()) != "sequence") {
    error = "does not contain a sequence node";
    return false;
  }
  pugi::xml_node img = seq.child("img");
  if (!img) {
    error = "does not contain an img node";
    return false;
  }
  pugi::xml_node list = seq.child("framelist");
  if (!list) {
    error = "does not contain a framelist node";
    return false;
  }
  out = SheetData{};
  out.image = img.attribute("name").as_string();
  out.mask = img.attribute("mask").as_string();
  bool warnXform = false, warnAnchor = false;
  SheetFrame prev;
  for (pugi::xml_node f : list.children("frame")) {
    SheetFrame fr;
    fr.src = {f.attribute("x").as_int(), f.attribute("y").as_int(), f.attribute("w").as_int(), f.attribute("h").as_int()};
    // An empty registration attribute copies the previous frame's (0 for the first).
    fr.reg.x = f.attribute("regx").empty() ? prev.reg.x : f.attribute("regx").as_int();
    fr.reg.y = f.attribute("regy").empty() ? prev.reg.y : f.attribute("regy").as_int();
    if (!f.attribute("xform").empty()) warnXform = true;
    if (f.child("anchor")) warnAnchor = true;
    out.frames.push_back(fr);
    prev = fr;
  }
  if (out.frames.empty()) {
    error = "does not contain any frames!";
    return false;
  }
  if (warnXform) out.warnings.push_back("frame xform is not supported");
  if (warnAnchor) out.warnings.push_back("frame anchor is not supported");
  if (pugi::xml_node sc = seq.child("script")) {
    out.script = sc.attribute("luascript").as_string();
    if (out.script.empty()) out.script = sc.text().get();
  }
  if (out.script.empty()) out.script = defaultScript(out.frames.size());
  return true;
}

}  // namespace choco::gfx
