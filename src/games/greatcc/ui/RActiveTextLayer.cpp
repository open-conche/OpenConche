#include "games/greatcc/ui/RActiveTextLayer.h"

namespace choco::games::greatcc {

void RActiveTextLayer::adoptText(std::unique_ptr<ActiveText> t) {
  if (t) texts_.push_back(std::move(t));
}

bool RActiveTextLayer::onAnimate() {
  RWindow::onAnimate();
  const double dt = deltaMs();
  // A text added while another updates is kept and first updated on the next tick.
  const std::size_t n = texts_.size();
  std::vector<std::unique_ptr<ActiveText>> keep;
  keep.reserve(n);
  std::vector<std::unique_ptr<ActiveText>> all = std::move(texts_);
  texts_.clear();
  for (std::size_t i = 0; i < all.size(); ++i)
    if (all[i]->update(dt)) keep.push_back(std::move(all[i]));
  for (auto& added : texts_) keep.push_back(std::move(added));
  texts_ = std::move(keep);
  return true;
}

void RActiveTextLayer::draw(ui::DrawContext& ctx) const {
  RWindow::draw(ctx);
  for (const auto& t : texts_) t->draw(ctx);
}

}  // namespace choco::games::greatcc
