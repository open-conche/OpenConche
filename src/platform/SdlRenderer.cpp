#include "platform/SdlRenderer.h"
#include "app/Diagnostics.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <vector>

#include "gfx/Quad.h"
#include "platform/Window.h"

namespace choco::platform {

struct SdlRenderer::Impl {
  SDL_Renderer* renderer = nullptr;
  SDL_BlendMode subtract = SDL_BLENDMODE_INVALID;
  void* white = nullptr;  // 1x1 white SDL_Texture: untextured geometry goes through it so every backend blends alike
  SDL_TextureAccess access = SDL_TEXTUREACCESS_STATIC;
  Window* window = nullptr;
  // The blend mode each live texture currently has. Textures are made BLEND + LINEAR and the scale mode is never
  // changed, so only the blend mode needs setting before a draw, and only when it differs. Every SDL setter costs an
  // object-table lookup.
  std::unordered_map<SDL_Texture*, SDL_BlendMode> blend;
  // Consecutive quads with the same texture and blend mode wait here and go out as one SDL_RenderGeometry call.
  // Draw order is untouched: anything else (a different state, a clip change, a fill, the end of the frame) flushes first.
  SDL_Texture* batchTex = nullptr;
  SDL_BlendMode batchMode = SDL_BLENDMODE_INVALID;
  std::vector<SDL_Vertex> batchVerts;
  std::vector<int> batchIdx;
};

namespace {

constexpr std::size_t kMaxBatchVertices = 16384;

SDL_BlendMode blendMode(gfx::Blend b, SDL_BlendMode subtract) {
  switch (b) {
    case gfx::Blend::Normal: return SDL_BLENDMODE_BLEND;
    case gfx::Blend::Additive: return SDL_BLENDMODE_ADD;
    case gfx::Blend::Multiply: return SDL_BLENDMODE_MUL;
    case gfx::Blend::Subtract: return subtract;
    case gfx::Blend::Opaque: return SDL_BLENDMODE_NONE;
  }
  return SDL_BLENDMODE_BLEND;
}

// Blend setters: if the renderer rejects a mode, say so once per mode and use Normal instead.
void setDrawBlend(SDL_Renderer* r, SDL_BlendMode mode) {
  if (SDL_SetRenderDrawBlendMode(r, mode)) return;
  static std::set<SDL_BlendMode> warned;
  if (warned.insert(mode).second) diag::err("[gfx] blend mode 0x%x not supported (%s); using Normal\n", static_cast<unsigned>(mode), SDL_GetError());
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void setTextureBlend(SDL_Texture* t, SDL_BlendMode mode) {
  if (SDL_SetTextureBlendMode(t, mode)) return;
  static std::set<SDL_BlendMode> warned;
  if (warned.insert(mode).second) diag::err("[gfx] blend mode 0x%x not supported (%s); using Normal\n", static_cast<unsigned>(mode), SDL_GetError());
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

SDL_FColor toSdl(gfx::ColorF c) { return SDL_FColor{c.r, c.g, c.b, c.a}; }

SDL_Vertex makeVertex(float x, float y, gfx::ColorF c, float u, float v) {
  SDL_Vertex out;
  out.position = SDL_FPoint{x, y};
  out.color = toSdl(c);
  out.tex_coord = SDL_FPoint{u, v};
  return out;
}

}  // namespace

SdlRenderer::SdlRenderer(Window& window) : impl_(std::make_unique<Impl>()) {
  impl_->renderer = static_cast<SDL_Renderer*>(window.rendererHandle());
  // Destination minus source*alpha, destination alpha kept. Colour and alpha operations are the same because the GL
  // backend rejects custom modes where they differ.
  impl_->subtract = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                               SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT);
  // SDL's software renderer RLE-encodes every STATIC texture (SDL_render_sw.c, SW_CreateTexture). Its scaled blit into a
  // clipped destination (SDL_BlitSurfaceClippedScaled, SDL_surface.c) then temporarily rewrites src->pixels and restores
  // the stale pointer afterwards, and the encoded surface is left with no pixels and no lock flag: every later blit of that
  // texture fails with "Invalid parameter src" and the sprite vanishes for good. A clipped, scaled 1:1 sprite is exactly
  // what a letterboxed window with a UI clip produces (the Options slider knob after the Full Screen toggle). STREAMING
  // textures are not RLE-encoded, so the software renderer gets those; GPU renderers keep STATIC.
  const char* name = SDL_GetRendererName(impl_->renderer);
  if (name && SDL_strcmp(name, "software") == 0) impl_->access = SDL_TEXTUREACCESS_STREAMING;
  impl_->window = &window;
  window.setFlushHook([this] { flush(); });
  deviceReset();
}

void SdlRenderer::deviceReset() {
  flush();
  impl_->blend.clear();  // the old handles are dead; their addresses may be reused
  // After a device reset the old handle is dead but must still be destroyed.
  release(impl_->white);
  static constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
  impl_->white = upload(kWhite, 1, 1);
}

SdlRenderer::~SdlRenderer() {
  flush();
  impl_->window->setFlushHook({});
  release(impl_->white);
}

void SdlRenderer::flush() {
  Impl& d = *impl_;
  if (!d.batchTex || d.batchVerts.empty()) {
    d.batchTex = nullptr;
    return;
  }
  auto cur = d.blend.find(d.batchTex);
  if (cur == d.blend.end() || cur->second != d.batchMode) {
    setTextureBlend(d.batchTex, d.batchMode);
    d.blend[d.batchTex] = d.batchMode;
  }
  if (!SDL_RenderGeometry(d.renderer, d.batchTex, d.batchVerts.data(), static_cast<int>(d.batchVerts.size()),
                          d.batchIdx.data(), static_cast<int>(d.batchIdx.size()))) {
    diag::err("[gfx] SDL_RenderGeometry failed: %s\n", SDL_GetError());
  }
  d.batchTex = nullptr;
  d.batchVerts.clear();
  d.batchIdx.clear();
}

void SdlRenderer::applyClip() {
  flush();
  if (!clips_.active()) {
    SDL_SetRenderClipRect(impl_->renderer, nullptr);
    return;
  }
  gfx::RectI c = clips_.current();
  SDL_Rect r{c.x, c.y, c.w, c.h};
  SDL_SetRenderClipRect(impl_->renderer, &r);
}

void SdlRenderer::pushClip(gfx::RectI logical) {
  clips_.push(logical);
  applyClip();
}

void SdlRenderer::popClip() {
  clips_.pop();
  applyClip();
}

float SdlRenderer::outputScale() const {
  SDL_FRect r{};
  if (SDL_GetRenderLogicalPresentationRect(impl_->renderer, &r) && r.w > 0) return r.w / static_cast<float>(kLogicalWidth);
  return 1.0f;
}

gfx::PointF SdlRenderer::outputOrigin() const {
  SDL_FRect r{};
  if (SDL_GetRenderLogicalPresentationRect(impl_->renderer, &r)) return {r.x, r.y};
  return {0, 0};
}

gfx::PointF SdlRenderer::outputScaleXY() const {
  SDL_FRect r{};
  if (SDL_GetRenderLogicalPresentationRect(impl_->renderer, &r) && r.w > 0 && r.h > 0) {
    return {r.w / static_cast<float>(kLogicalWidth), r.h / static_cast<float>(kLogicalHeight)};
  }
  return {1, 1};
}

void SdlRenderer::drawGeometry(const gfx::Texture* tex, std::span<const gfx::Vertex> vertices, std::span<const int> indices,
                               gfx::Blend blend) {
  if (clips_.blocksAll() || vertices.empty() || indices.empty()) return;  // an empty SDL clip rect would mean "no clip"
  Impl& d = *impl_;
  const SDL_BlendMode mode = blendMode(blend, d.subtract);
  // Untextured geometry samples the 1x1 white texture (the software renderer ignores blend modes for untextured
  // triangles). UVs vary with position because the software blitter mishandles triangles with degenerate UVs.
  SDL_Texture* st = static_cast<SDL_Texture*>(tex ? tex->handle() : d.white);
  if (!st) return;
  if (st != d.batchTex || mode != d.batchMode || d.batchVerts.size() + vertices.size() > kMaxBatchVertices) {
    flush();
    d.batchTex = st;
    d.batchMode = mode;
  }
  const int base = static_cast<int>(d.batchVerts.size());
  for (const auto& v : vertices) {
    SDL_Vertex out = makeVertex(v.pos.x, v.pos.y, v.color, v.uv.x, v.uv.y);
    if (!tex) out.tex_coord = SDL_FPoint{v.pos.x / 800.0f, v.pos.y / 600.0f};
    d.batchVerts.push_back(out);
  }
  for (int i : indices) d.batchIdx.push_back(base + i);
}

// Axis-aligned fills use SDL_RenderFillRect: unlike untextured SDL_RenderGeometry it applies the blend mode in the
// software renderer, and it keeps edges exactly on pixel boundaries.
void SdlRenderer::fillRect(gfx::RectF r, gfx::ColorF c, gfx::Blend blend) {
  if (clips_.blocksAll()) return;
  flush();
  setDrawBlend(impl_->renderer, blendMode(blend, impl_->subtract));
  SDL_SetRenderDrawColorFloat(impl_->renderer, c.r, c.g, c.b, c.a);
  SDL_FRect f{r.x, r.y, r.w, r.h};
  SDL_RenderFillRect(impl_->renderer, &f);
}

void SdlRenderer::drawTexture(const gfx::Texture& tex, gfx::RectF src, gfx::RectF dst, gfx::ColorF tint, gfx::Blend blend,
                              float rotationDeg, gfx::PointF origin, bool hflip, bool vflip) {
  if (tex.width() <= 0 || tex.height() <= 0) return;
  const auto v = gfx::buildTexturedQuad(src, dst, tex.width(), tex.height(), tint, rotationDeg, origin, hflip, vflip);
  static constexpr int idx[6] = {0, 1, 2, 0, 2, 3};
  drawGeometry(&tex, v, idx, blend);
}

std::unique_ptr<gfx::RenderTarget> SdlRenderer::createTarget(int, int) {
  diag::err("[gfx] createTarget: not implemented\n");
  return nullptr;
}

void SdlRenderer::setTarget(gfx::RenderTarget*) { diag::err("[gfx] setTarget: not implemented\n"); }

void* SdlRenderer::upload(const std::uint8_t* rgba, int w, int h) {
  SDL_Texture* t = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, impl_->access, w, h);
  if (!t) {
    diag::err("[gfx] SDL_CreateTexture failed: %s\n", SDL_GetError());
    return nullptr;
  }
  if (!SDL_UpdateTexture(t, nullptr, rgba, w * 4)) {
    diag::err("[gfx] SDL_UpdateTexture failed: %s\n", SDL_GetError());
    SDL_DestroyTexture(t);
    return nullptr;
  }
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  impl_->blend[t] = SDL_BLENDMODE_BLEND;
  return t;
}

bool SdlRenderer::update(void* handle, int x, int y, int w, int h, const std::uint8_t* rgba, int stride) {
  if (!handle) return false;
  const SDL_Rect r{x, y, w, h};
  if (SDL_UpdateTexture(static_cast<SDL_Texture*>(handle), &r, rgba, stride)) return true;
  diag::err("[gfx] SDL_UpdateTexture (rect) failed: %s\n", SDL_GetError());
  return false;
}

void SdlRenderer::release(void* handle) {
  if (!handle) return;
  auto* t = static_cast<SDL_Texture*>(handle);
  if (impl_->batchTex == t) flush();  // queued quads still point at it
  impl_->blend.erase(t);
  SDL_DestroyTexture(t);
}

}  // namespace choco::platform
