#include "gfx/AnmSheet.h"

#include <cstdint>
#include <cstring>

namespace choco::gfx {

namespace {

constexpr float kXformSentinel =
    -10000000.0f; // the never-real float the writer pads every xform with

struct Reader {
  const char *p;
  std::size_t n, pos = 0;
  std::string *error;

  bool u32(std::uint32_t &out) {
    if (pos + 4 > n)
      return fail("the file ends inside a u32");
    std::memcpy(&out, p + pos, 4);
    pos += 4;
    return true;
  }
  bool i32(std::int32_t &out) {
    std::uint32_t v;
    if (!u32(v))
      return false;
    std::memcpy(&out, &v, 4);
    return true;
  }
  bool tag(const char *want) {
    if (pos + 4 > n)
      return fail("the file ends inside a tag");
    if (std::memcmp(p + pos, want, 4) != 0)
      return fail(std::string("expected the tag '") + want + "'");
    pos += 4;
    return true;
  }
  bool str(std::string &out, std::uint32_t *rawSize = nullptr) {
    std::uint32_t len;
    if (!u32(len))
      return false;
    if (rawSize)
      *rawSize = len;
    if (pos + len > n)
      return fail("the file ends inside a string");
    out.assign(p + pos, len);
    pos += len;
    return true;
  }
  bool fail(const std::string &what) {
    if (error->empty())
      *error = what;
    return false;
  }
};

} // namespace

bool AnmSheet::parse(std::string_view bytes, SheetData &out,
                     std::string &error) {
  out = SheetData{};
  Reader r{bytes.data(), bytes.size(), 0, &error};
  std::uint32_t version = 0, count = 0;
  if (!r.tag("ANIM") || !r.u32(version) || !r.u32(count) || version != 1) {
    if (error.empty())
      error = "not an ANIM version 1 file";
    return false;
  }
  std::string buildPath, sourcePath;
  if (!r.str(buildPath) || !r.str(sourcePath))
    return false; // never read again

  if (!r.tag("IMGN"))
    return false;
  std::int32_t values[8];
  for (std::int32_t &v : values)
    if (!r.i32(v))
      return false;
  out.reg = {values[0], values[1]};
  for (int i = 4; i < 8; ++i) {
    float f;
    std::memcpy(&f, &values[i], 4);
    if (f != kXformSentinel)
      out.warnings.push_back(
          "the sheet header carries a real xform, which is not supported");
  }
  if (!r.str(out.image) || !r.str(out.mask))
    return false;
  if (!out.mask.empty())
    out.warnings.push_back("the mask image '" + out.mask + "' is not applied");

  if (!r.tag("FRMS"))
    return false;
  std::uint32_t frames = 0;
  if (!r.u32(frames))
    return false;
  out.frames.reserve(frames);
  for (std::uint32_t i = 0; i < frames; ++i) {
    std::int32_t f[12];
    for (std::int32_t &v : f)
      if (!r.i32(v))
        return false;
    for (int k = 6; k < 12; ++k) {
      float xform;
      std::memcpy(&xform, &f[k], 4);
      if (xform != kXformSentinel)
        out.warnings.push_back("frame " + std::to_string(i) +
                               " carries a real xform, which is not supported");
    }
    SheetFrame frame;
    frame.reg = {f[0], f[1]};
    frame.src = {f[2], f[3], f[4], f[5]};
    std::string name; // the frame's source-file name; TAnimatedTexture keeps
                      // it, the engine has no use for it yet
    if (!r.str(name))
      return false;
    std::uint32_t anchors = 0;
    if (!r.u32(anchors))
      return false;
    frame.anchors.reserve(anchors);
    for (std::uint32_t a = 0; a < anchors; ++a) {
      std::uint32_t x = 0, y = 0;
      std::string anchorName;
      std::uint32_t rawLen = 0;
      if (!r.u32(x) || !r.u32(y) || !r.str(anchorName, &rawLen))
        return false;
      // Stored unsigned, read as signed coordinates by GetAnchorPoint's
      // callers.
      std::int32_t sx = static_cast<std::int32_t>(x),
                   sy = static_cast<std::int32_t>(y);
      frame.anchors.push_back({std::move(anchorName), {sx, sy}});
    }
    out.frames.push_back(std::move(frame));
  }
  if (out.frames.empty()) {
    error = "the sheet has no frames";
    return false;
  }

  if (r.pos + 4 <= r.n && std::memcmp(bytes.data() + r.pos, "SCRP", 4) == 0) {
    r.pos += 4;
    if (!r.str(out.script))
      return false;
  } else {
    out.script = SpriteSheet::defaultScript(out.frames.size());
  }
  if (r.pos != r.n)
    out.warnings.push_back("trailing bytes after the sheet are ignored");
  return true;
}

} // namespace choco::gfx
