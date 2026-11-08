#include "gfx/RecordingRenderer.h"

#include <algorithm>

namespace choco::gfx {

RecordingRenderer::Call& RecordingRenderer::add(Call::Kind k) {
  Call& c = calls_.emplace_back();
  c.kind = k;
  c.clipped = clips_.active();
  if (c.clipped) c.activeClip = clips_.current();
  return c;
}

std::size_t RecordingRenderer::count(Call::Kind k) const {
  return static_cast<std::size_t>(std::count_if(calls_.begin(), calls_.end(), [k](const Call& c) { return c.kind == k; }));
}

void RecordingRenderer::pushClip(RectI logical) {
  RectI eff = clips_.push(logical);
  Call& c = add(Call::Kind::PushClip);
  c.clip = logical;
  c.effectiveClip = eff;
}

void RecordingRenderer::popClip() {
  clips_.pop();
  add(Call::Kind::PopClip);
}

void RecordingRenderer::drawTexture(const Texture& tex, RectF src, RectF dst, ColorF tint, Blend blend, float rotationDeg,
                                    PointF origin, bool hflip, bool vflip) {
  Call& c = add(Call::Kind::DrawTexture);
  c.texture = &tex;
  c.src = src;
  c.dst = dst;
  c.color = tint;
  c.blend = blend;
  c.rotation = rotationDeg;
  c.origin = origin;
  c.hflip = hflip;
  c.vflip = vflip;
}

void RecordingRenderer::drawGeometry(const Texture* tex, std::span<const Vertex> vertices, std::span<const int> indices,
                                     Blend blend) {
  Call& c = add(Call::Kind::DrawGeometry);
  c.texture = tex;
  c.blend = blend;
  c.vertices.assign(vertices.begin(), vertices.end());
  c.indices.assign(indices.begin(), indices.end());
}

void RecordingRenderer::fillRect(RectF rect, ColorF color, Blend blend) {
  Call& c = add(Call::Kind::FillRect);
  c.dst = rect;
  c.color = color;
  c.blend = blend;
}

std::unique_ptr<RenderTarget> RecordingRenderer::createTarget(int, int) {
  add(Call::Kind::CreateTarget);
  return nullptr;
}

void RecordingRenderer::setTarget(RenderTarget*) { add(Call::Kind::SetTarget); }

}  // namespace choco::gfx
