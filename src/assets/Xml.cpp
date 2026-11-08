#include "assets/Xml.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <stb_image.h>  // stbi_zlib_decode_malloc (declaration only; the implementation is in gfx/Image.cpp)

namespace choco::assets {

std::string collapseWhitespace(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  bool pending = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
      pending = !out.empty();
    } else {
      if (pending) out += ' ';
      pending = false;
      out += c;
    }
  }
  return out;
}

namespace {
void gather(const pugi::xml_node& n, std::string& out) {
  for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
    if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
      out += c.value();
    } else if (c.type() == pugi::node_element) {
      out += ' ';  // element boundaries separate tokens
      gather(c, out);
      out += ' ';
    }
  }
}
}  // namespace

std::string nodeText(const pugi::xml_node& n) {
  std::string raw;
  gather(n, raw);
  return collapseWhitespace(raw);
}

std::string childText(const pugi::xml_node& n, const char* name, bool* found) {
  pugi::xml_node c = n.child(name);
  if (found) *found = static_cast<bool>(c);
  return c ? nodeText(c) : std::string();
}

bool Xml::finish(const pugi::xml_parse_result& r) {
  error_.clear();
  if (!r) {
    error_ = std::string("XML parse error: ") + r.description() + " at offset " + std::to_string(r.offset);
    return false;
  }
  if (!doc_.document_element()) {
    error_ = "XML document has no root element";
    return false;
  }
  return true;
}

bool Xml::parse(const Bytes& data) {
  // "ZXML" wrapper (the vanilla archive and settings.xml): magic, u32 compressed length, u32 raw length, zlib stream.
  if (data.size() >= 12 && std::memcmp(data.data(), "ZXML", 4) == 0) {
    auto u32 = [&](size_t o) {
      return uint32_t(data[o]) | uint32_t(data[o + 1]) << 8 | uint32_t(data[o + 2]) << 16 | uint32_t(data[o + 3]) << 24;
    };
    const size_t compLen = std::min<size_t>(u32(4), data.size() - 12);
    const size_t rawLen = u32(8);
    constexpr size_t kMaxRaw = 64u << 20;  // the largest shipped file is well under 1 MB
    const char* comp = reinterpret_cast<const char*>(data.data()) + 12;
    if (rawLen > kMaxRaw) {
      error_ = "ZXML: raw length too large";
      return false;
    }
    bool ok = false;
    if (rawLen != 0) {
      // The original inflates into a buffer of exactly the declared size (zlib uncompress), so bound ours the same way.
      std::string raw(rawLen, '\0');
      const int n = stbi_zlib_decode_buffer(raw.data(), int(rawLen), comp, int(compLen));
      if (n < 0 || size_t(n) != rawLen) {
        error_ = "ZXML: bad zlib data";
        return false;
      }
      ok = finish(doc_.load_buffer(raw.data(), rawLen, pugi::parse_default, pugi::encoding_auto));
    } else {
      int outLen = 0;
      char* raw = stbi_zlib_decode_malloc(comp, int(compLen), &outLen);
      if (!raw) {
        error_ = "ZXML: bad zlib data";
        return false;
      }
      ok = finish(doc_.load_buffer(raw, size_t(outLen), pugi::parse_default, pugi::encoding_auto));
      std::free(raw);
    }
    return ok;
  }
  return finish(doc_.load_buffer(data.data(), data.size(), pugi::parse_default, pugi::encoding_auto));
}

bool Xml::parse(std::string_view text) {
  if (text.size() >= 4 && text.substr(0, 4) == "ZXML") {
    Bytes bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return parse(bytes);
  }
  return finish(doc_.load_buffer(text.data(), text.size(), pugi::parse_default, pugi::encoding_utf8));
}

}  // namespace choco::assets
