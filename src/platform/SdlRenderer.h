#pragma once

#include "gfx/ClipStack.h"
#include "gfx/Renderer.h"
#include "gfx/TextureCache.h"

namespace choco::platform {

class Window;

// gfx::Renderer on the window's SDL renderer. Everything goes through SDL_RenderGeometry. Coordinates are logical
// (the window sets the 800x600 letterbox presentation); clips are logical too. Also the TextureUploader for the cache.
// Must not outlive the Window. No Texture uploaded through it may outlive this renderer (textures release their GPU
// handle through it when destroyed).
class SdlRenderer : public gfx::Renderer, public gfx::TextureUploader {
 public:
  explicit SdlRenderer(Window& window);
  ~SdlRenderer() override;

  void pushClip(gfx::RectI logical) override;
  void popClip() override;
  void drawTexture(const gfx::Texture& tex, gfx::RectF src, gfx::RectF dst, gfx::ColorF tint, gfx::Blend blend,
                   float rotationDeg, gfx::PointF origin, bool hflip, bool vflip) override;
  void drawGeometry(const gfx::Texture* tex, std::span<const gfx::Vertex> vertices, std::span<const int> indices,
                    gfx::Blend blend) override;
  void fillRect(gfx::RectF rect, gfx::ColorF color, gfx::Blend blend) override;
  float outputScale() const override;
  gfx::PointF outputOrigin() const override;
  gfx::PointF outputScaleXY() const override;
  std::unique_ptr<gfx::RenderTarget> createTarget(int w, int h) override;
  void setTarget(gfx::RenderTarget* target) override;

  // Call after SDL reports a device reset, before TextureCache::reuploadAll: re-creates the renderer's own resources.
  void deviceReset();
  // Sends the queued quads to SDL. Rule: anything that draws, clears, reads or presents through SDL directly, without
  // going through this class, must call flush() first or its output lands out of order. Window does so itself.
  void flush();

  void* upload(const std::uint8_t* rgba, int w, int h) override;
  void release(void* handle) override;
  bool update(void* handle, int x, int y, int w, int h, const std::uint8_t* rgba, int stride) override;

 private:
  struct Impl;
  void applyClip();
  std::unique_ptr<Impl> impl_;
  gfx::ClipStack clips_;
};

}  // namespace choco::platform
