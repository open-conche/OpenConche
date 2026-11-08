#pragma once

#include <memory>
#include <span>

#include "gfx/Texture.h"
#include "gfx/Types.h"

namespace choco::gfx {

// Off-screen drawing surface. Not implemented in this slice.
class RenderTarget {
 public:
  virtual ~RenderTarget() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
};

// Draws in logical 800x600 coordinates. Implementations: platform::SdlRenderer, gfx::RecordingRenderer.
class Renderer {
 public:
  virtual ~Renderer() = default;

  // Restricts drawing to `logical` intersected with the clip already in force. Pairs with popClip.
  virtual void pushClip(RectI logical) = 0;
  virtual void popClip() = 0;

  // src is in logical texture units (w <= 0 or h <= 0 means the whole texture; a proper sub-rectangle is inset by half a texel, see Quad.h), dst in logical screen units. The
  // result is rotated by rotationDeg (clockwise) around dst.xy + origin, after flipping inside dst. tint multiplies
  // the texture colour and alpha.
  virtual void drawTexture(const Texture& tex, RectF src, RectF dst, ColorF tint, Blend blend, float rotationDeg = 0,
                           PointF origin = {}, bool hflip = false, bool vflip = false) = 0;
  // Triangle list; tex may be null for plain coloured geometry. Used by glyphs and particles.
  virtual void drawGeometry(const Texture* tex, std::span<const Vertex> vertices, std::span<const int> indices,
                            Blend blend) = 0;
  virtual void fillRect(RectF rect, ColorF color, Blend blend) = 0;

  // Output pixels per logical pixel, for rasterising text at the right size.
  virtual float outputScale() const = 0;
  // Where logical (0,0) lands in output pixels (the letterbox offset, possibly a half pixel) and the output pixels
  // per logical pixel on each axis. Text uses them to snap glyphs to whole output pixels.
  virtual PointF outputOrigin() const { return {0, 0}; }
  virtual PointF outputScaleXY() const { return {outputScale(), outputScale()}; }

  // Later slice. May return null / do nothing and log.
  virtual std::unique_ptr<RenderTarget> createTarget(int w, int h) = 0;
  virtual void setTarget(RenderTarget* target) = 0;
};

}  // namespace choco::gfx
