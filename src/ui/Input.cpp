#include "ui/Input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::ui {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool eraseFirst(std::string& s, const char* part) {
  const auto at = s.find(part);
  if (at == std::string::npos) return false;
  s.erase(at, std::char_traits<char>::length(part));
  return true;
}

// ---- hit testing ----

bool contains(const Window& w, int px, int py) {
  const gfx::RectI& r = w.rect();
  return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// The first (frontmost) shown child at or after `from` whose rectangle holds the point (in `p`'s coordinates).
Window* childAt(const Window& p, std::size_t from, int px, int py) {
  const auto& kids = p.children();
  for (std::size_t i = from; i < kids.size(); ++i)
    if (kids[i]->shown() && contains(*kids[i], px, py)) return kids[i].get();
  return nullptr;
}

// Descends through the first shown child under the point to the deepest one. (px, py) is in w's own coordinates.
Window* deepestAt(Window* w, int px, int py) {
  while (Window* c = childAt(*w, 0, px, py)) {
    px -= c->rect().x;
    py -= c->rect().y;
    w = c;
  }
  return w;
}

enum class Kind { Down, Up, Move };

bool invoke(Kind k, Window& w, int lx, int ly) {
  switch (k) {
    case Kind::Down: return w.onMouseDown(lx, ly);
    case Kind::Up: return w.onMouseUp(lx, ly);
    case Kind::Move: return w.onMouseMove(lx, ly);
  }
  return false;
}

bool inTopModal(const Window* w, const Window* top) {
  for (; w; w = w->parent()) {
    if (w == top) return true;
    if (w->isModal()) return false;
  }
  return false;
}

}  // namespace

std::string normalizeKeyString(const std::string& key) {
  std::string rest = lower(key);
  if (rest.size() > 1 && rest[0] == '@') return rest;
  std::string out;
  if (eraseFirst(rest, "shift-")) out += "shift-";
  if (eraseFirst(rest, "control-") || eraseFirst(rest, "ctrl-")) out += "ctrl-";
  if (eraseFirst(rest, "alt-")) out += "alt-";
  if (!rest.empty()) out += static_cast<char>(std::toupper(static_cast<unsigned char>(rest[0])));
  return out;
}

char keyChar(const KeyEvent& ev) {
  if (ev.name.size() == 1) return static_cast<char>(std::tolower(static_cast<unsigned char>(ev.name[0])));
  if (ev.name == "Return" || ev.name == "Keypad Enter") return '\r';
  if (ev.name == "Escape") return 27;
  if (ev.name == "Space") return ' ';
  if (ev.name == "Tab") return '\t';
  if (ev.name == "Backspace") return 8;
  return 0;
}

std::string hotkeyOf(const KeyEvent& ev, bool withModifiers) {
  if (ev.name.size() != 1) return "@" + lower(ev.name);
  std::string s;
  if (withModifiers) {
    if (ev.shift) s += "shift-";
    if (ev.ctrl) s += "ctrl-";
    if (ev.alt) s += "alt-";
  }
  return s + ev.name;
}

// ---- the manager's side ----

gfx::PointF WindowManager::screenOrigin(const Window& w) {
  float x = 0, y = 0;
  for (const Window* p = &w; p; p = p->parent()) {
    x += static_cast<float>(p->rect().x);
    y += static_cast<float>(p->rect().y);
    if (p->isModal()) break;
  }
  return {x, y};
}

void WindowManager::setFocus(WindowId id) {
  Window* w = window(id);
  const WindowId old = focus_;
  focus_ = w ? id : 0;
  if (w) w->onSetFocus(old);
}

bool WindowManager::textInputWanted(gfx::RectI& area) const {
  const Window* w = window(focus_);
  if (!w || w->isDestroyed() || !w->wantsTextInput()) return false;
  const gfx::PointF o = screenOrigin(*w);
  area = {static_cast<int>(o.x), static_cast<int>(o.y), w->rect().w, w->rect().h};
  return true;
}

void WindowManager::addMouseListener(WindowId id) {
  if (!isMouseListener(id)) mouseListeners_.push_back(id);
}

void WindowManager::removeMouseListener(WindowId id) {
  mouseListeners_.erase(std::remove(mouseListeners_.begin(), mouseListeners_.end(), id), mouseListeners_.end());
}

bool WindowManager::isMouseListener(WindowId id) const {
  return std::find(mouseListeners_.begin(), mouseListeners_.end(), id) != mouseListeners_.end();
}

// A modal push or pop: hover is cleared and every captured window is told the mouse left.
void WindowManager::cancelMouseState() {
  // The hovered window is told the mouse left first (a button then drops its glow), then every captured window.
  const WindowId hovered = std::exchange(hover_, 0);
  if (Window* h = window(hovered); h && !isMouseListener(hovered)) h->onMouseLeave();
  std::vector<WindowId> ids;
  ids.swap(mouseListeners_);
  for (WindowId id : ids)
    if (Window* w = window(id)) w->onMouseLeave();
  mouseListeners_.clear();
}

void WindowManager::postPressAnyKey(const std::string& name) {
  if (overlayMouse_ && overlayMouse_()) return;  // `overlaymouse` set: the original posts nothing
  post({kPressAnyKey, name, 0});
}

void WindowManager::handleInput(const InputEvent& ev) {
  if (ev.type == InputEvent::Type::Key) routeKey(ev);
  else if (ev.type == InputEvent::Type::Text) routeText(ev);
  else routeMouse(ev);
}

void WindowManager::routeMouse(const InputEvent& ev) {
  Kind k = Kind::Move;
  if (ev.type == InputEvent::Type::MouseDown) k = Kind::Down;
  else if (ev.type == InputEvent::Type::MouseUp) k = Kind::Up;
  if (k != Kind::Move && ev.button > 1) return;  // only the left button clicks
  const int sx = static_cast<int>(std::floor(ev.x)), sy = static_cast<int>(std::floor(ev.y));

  WindowId handledBy = 0;
  if (!ev.inArea) {
    if (k == Kind::Down) return;
    if (k == Kind::Move) {  // the pointer is in the letterbox bars: nothing is under it
      if (hover_ != 0) {
        Window* old = window(hover_);
        hover_ = 0;
        if (old) old->onMouseLeave();
      }
      return;
    }
  }

  // 1. Captured windows get the event first, in the order they were captured.
  if (!mouseListeners_.empty()) {
    const std::vector<WindowId> ids = mouseListeners_;
    for (WindowId id : ids) {
      Window* w = window(id);
      if (!w) {
        removeMouseListener(id);
        continue;
      }
      const gfx::PointF o = screenOrigin(*w);
      if (invoke(k, *w, sx - static_cast<int>(o.x), sy - static_cast<int>(o.y))) {
        handledBy = id;
        break;
      }
    }
  }

  // 2. The hit-test path: deepest first-child under the point, then fall through to the sibling behind, then the parent.
  if (handledBy == 0 && ev.inArea) {
    Window* top = modals_.back().get();
    const gfx::PointF to = screenOrigin(*top);
    Window* w = deepestAt(top, sx - static_cast<int>(to.x), sy - static_cast<int>(to.y));
    if (k == Kind::Down) {  // the nearest focusable ancestor of what was hit takes the focus
      Window* f = w;
      while (f && !f->focusable()) f = f->parent();
      if (f && f->id() != focus_) setFocus(f->id());
    }
    for (;;) {
      const gfx::PointF o = screenOrigin(*w);
      if (invoke(k, *w, sx - static_cast<int>(o.x), sy - static_cast<int>(o.y))) {
        handledBy = w->id();
        break;
      }
      if (w->isModal() || !w->parent()) break;
      Window* p = w->parent();
      const gfx::PointF po = screenOrigin(*p);
      const int px = sx - static_cast<int>(po.x), py = sy - static_cast<int>(po.y);
      std::size_t at = 0;
      while (at < p->children().size() && p->children()[at].get() != w) ++at;
      Window* behind = childAt(*p, at + 1, px, py);
      if (!behind) {
        w = p;
      } else if (legacyFallThroughPoint_) {
        // C2 TWindow::HandleEvent 0x3414f: the sibling's children are tested with the point NOT translated into the
        // sibling's space; deeper levels subtract only the found child's offset (0x34170).
        Window* c = childAt(*behind, 0, px, py);
        w = c ? deepestAt(c, px - c->rect().x, py - c->rect().y) : behind;
      } else {
        w = deepestAt(behind, px - behind->rect().x, py - behind->rect().y);
      }
    }
  }

  if (k == Kind::Move && hover_ != handledBy) {
    Window* old = window(hover_);
    hover_ = handledBy;
    if (old) old->onMouseLeave();
  }
  if (k == Kind::Up && handledBy == 0) postPressAnyKey("mouse:up");
}

// Typed text goes to the focus window and bubbles up to the modal; nobody consuming it is not an error.
void WindowManager::routeText(const InputEvent& ev) {
  Window* top = modals_.back().get();
  Window* start = window(focus_);
  if (!start || !inTopModal(start, top)) start = top;
  for (Window* w = start; w; w = w->parent()) {
    if (w->onText(ev.text)) return;
    if (w->isModal()) break;
  }
}

void WindowManager::routeKey(const InputEvent& ev) {
  if (ev.key.name == "Escape" && escape_) escape_();  // the loop-level Escape hook runs before any window sees the key
  Window* top = modals_.back().get();
  Window* start = window(focus_);
  if (!start || !inTopModal(start, top)) start = top;

  for (Window* w = start; w; w = w->parent()) {
    if (w->onKeyDown(ev.key)) return;
    if (w->isModal()) break;
  }
  const char c = keyChar(ev.key);
  postPressAnyKey(std::string("raw:") + (c ? std::string(1, c) : std::string()));

  // The character event follows the key; Enter, Escape and Tab matter to the windows.
  if (c != '\r' && c != 27 && c != '\t') return;
  for (Window* w = start; w; w = w->parent()) {
    if (w->onChar(ev.key)) return;
    if (w->isModal()) break;
  }
}

}  // namespace choco::ui
