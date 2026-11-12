#include "games/greatcc/ui/RScrollable.h"

#include <algorithm>

#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::games::greatcc {

ui::Window* RScrollable::interior() const { return manager().window(interior_); }
ui::Window* RScrollable::slider() const { return manager().window(slider_); }

void RScrollable::postChildrenInit(const ui::Style& style) {
  RWindow::postChildrenInit(style);
  vertical_ = style.booleanOr("vscroll", true);
  tick_ = static_cast<int>(style.numberOr("tick", 0));
  except_.clear();
  if (auto t = style.table("except"))
    for (int i = 1; i <= t->size(); ++i)
      if (auto n = t->stringAt(i)) except_.push_back(*n);

  auto interior = std::make_unique<RWindow>(manager());
  interior_ = interior->id();
  // RGiveToInterior over the direct children. The list is front first; adopting each at the interior's front from the back of
  // the list keeps their order.
  std::vector<ui::Window*> kids;
  for (const auto& c : children()) kids.push_back(c.get());
  for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
    ui::Window* c = *it;
    if (std::find(except_.begin(), except_.end(), c->name()) != except_.end()) continue;
    if (!slider_ && dynamic_cast<ui::SliderWindow*>(c)) {
      slider_ = c->id();  // the first slider drives the interior and stays a child of the scroller
      continue;
    }
    if (auto owned = orphan(*c)) interior->adoptFront(std::move(owned));
  }
  ui::fitToChildren(*interior);
  adoptFront(std::move(interior));
  // The interior goes behind the slider and the kept windows: bring them in front of it.
  std::vector<std::unique_ptr<ui::Window>> keep;
  while (children().size() > 1) keep.push_back(orphan(*children().back()));  // back to front
  for (auto& k : keep) adoptFront(std::move(k));                              // so the original order returns
}

void RScrollable::setInteriorPosition(float f) {
  ui::Window* in = interior();
  if (!in) return;
  f = std::clamp(f, 0.0f, 1.0f);
  pos_ = f;
  auto* sl = dynamic_cast<ui::SliderWindow*>(slider());
  if (sl) sl->setValue(f, true);
  const int viewLen = vertical_ ? rect().h : rect().w;
  const int inLen = vertical_ ? in->rect().h : in->rect().w;
  int offset = 0;
  if (inLen < viewLen) {
    pos_ = 0;
    if (sl) sl->setValue(0, true);
  } else {
    offset = -static_cast<int>(static_cast<float>(inLen - viewLen) * pos_);
    offset = std::max(offset, viewLen - inLen);
  }
  if (vertical_) in->setPos(in->rect().x, offset);
  else in->setPos(offset, in->rect().y);
}

void RScrollable::scrollBy(int dir) {
  const ui::Window* in = interior();
  if (!in || tick_ == 0) return;
  const int viewLen = vertical_ ? rect().h : rect().w;
  const int inLen = vertical_ ? in->rect().h : in->rect().w;
  if (inLen <= viewLen) return;
  setInteriorPosition(pos_ + dir * static_cast<float>(tick_) / static_cast<float>(inLen - viewLen));
}

bool RScrollable::onMessage(const ui::Message& m) {
  if (m.type != ui::kSliderValChanged) return false;
  auto* sl = dynamic_cast<ui::SliderWindow*>(slider());
  if (!sl) return false;
  setInteriorPosition(sl->value());
  return true;
}

std::string RScrollable::detail() const {
  return RWindow::detail() + (vertical_ ? " vscroll" : " hscroll") + " tick=" + std::to_string(tick_) +
         " pos=" + std::to_string(pos_);
}

}  // namespace choco::games::greatcc
