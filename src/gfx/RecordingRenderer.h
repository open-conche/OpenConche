#pragma once

#include <vector>

#include "gfx/ClipStack.h"
#include "gfx/Renderer.h"

namespace choco::gfx {

// Records every call, for draw-order tests. Draws nothing.
class RecordingRenderer : public Renderer {
 public:
  struct Call {
    enum class Kind { PushClip, PopClip, DrawTexture, DrawGeometry, FillRect, CreateTarget, SetTarget } kind{};
    RectI clip;            // PushClip: the rectangle asked for
    RectI effectiveClip;   // PushClip: after intersection
    const Texture* texture = nullptr;
    RectF src, dst;        // DrawTexture; FillRect keeps its rectangle in dst
    ColorF color;          // tint or fill colour
    Blend blend = Blend::Normal;
    float rotation = 0;
    PointF origin;
    bool hflip = false, vflip = false;
    std::vector<Vertex> vertices;  // DrawGeometry
    std::vector<int> indices;
    RectI activeClip;      // clip in force when a draw call was made
    bool clipped = false;  // whether any clip was in force
  };

  const std::vector<Call>& calls() const { return calls_; }
  void clear() { calls_.clear(); }
  std::size_t count(Call::Kind k) const;
  void setOutputScale(float s) { scale_ = s; }

  void pushClip(RectI logical) override;
  void popClip() override;
  void drawTexture(const Texture& tex, RectF src, RectF dst, ColorF tint, Blend blend, float rotationDeg, PointF origin,
                   bool hflip, bool vflip) override;
  void drawGeometry(const Texture* tex, std::span<const Vertex> vertices, std::span<const int> indices,
                    Blend blend) override;
  void fillRect(RectF rect, ColorF color, Blend blend) override;
  float outputScale() const override { return scale_; }
  PointF outputOrigin() const override { return origin_; }
  PointF outputScaleXY() const override { return {scale_, scale_ * yRatio_}; }
  void setOutputOrigin(PointF o) { origin_ = o; }
  void setYRatio(float r) { yRatio_ = r; }
  std::unique_ptr<RenderTarget> createTarget(int w, int h) override;
  void setTarget(RenderTarget* target) override;

 private:
  Call& add(Call::Kind k);
  std::vector<Call> calls_;
  ClipStack clips_;
  float scale_ = 1;
  float yRatio_ = 1;
  PointF origin_;
};

}  // namespace choco::gfx
