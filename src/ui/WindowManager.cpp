#include "ui/WindowManager.h"

#include <algorithm>
#include <cstdio>

#include "sdk/Sprite.h"
#include "ui/widgets/CreditsWindow.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::ui {

WindowManager::WindowManager() {
  auto screen = std::make_unique<Screen>(*this);
  creationStack_.push_back(screen->id());
  focus_ = screen->id();
  modals_.push_back(std::move(screen));
  registerBuiltinWidgets(*this);
  registerTextWidgets(*this);
  registerInputWidgets(*this);
  registerCreditsWindow(*this);
}

WindowManager::~WindowManager() { destroyAll(); }

void WindowManager::destroyAll() {
  // Children first, then the modals from the top, so no parent pointer dangles while a window dies.
  destroyed_.clear();
  while (!modals_.empty()) modals_.pop_back();
}

void WindowManager::addWindowType(std::string name, Factory make) {
  if (!factories_.count(name)) types_.push_back(name);
  Factory& slot = factories_[std::move(name)];
  if (make) slot = std::move(make);  // AddWindowType from Lua (an empty factory) must not erase a registered one
}

std::unique_ptr<Window> WindowManager::createWindow(const std::string& typeName) {
  auto it = factories_.find(typeName);
  if (it != factories_.end() && it->second) return it->second(*this);
  auto b = builtins_.find(typeName);
  if (b != builtins_.end()) return b->second(*this);
  return std::make_unique<PlaceholderWindow>(*this, typeName.empty() ? "Window" : typeName);
}

Window* WindowManager::window(WindowId id) const {
  auto it = byId_.find(id);
  if (it == byId_.end()) return nullptr;
  for (Window* w = it->second; w; w = w->parent())
    if (w->isDestroyed()) return nullptr;
  return it->second;
}

Screen* WindowManager::screen() const { return static_cast<Screen*>(modals_.front().get()); }

Window* WindowManager::findInTopModal(std::string_view name) const { return modals_.back()->find(name); }

Window* WindowManager::findInChain(std::string_view name) const {
  for (auto it = modals_.rbegin(); it != modals_.rend(); ++it)
    if (Window* w = (*it)->find(name)) return w;
  return nullptr;
}

Window* WindowManager::creationTop() const {
  for (auto it = creationStack_.rbegin(); it != creationStack_.rend(); ++it)
    if (Window* w = window(*it)) return w;
  return modals_.back().get();
}

Window& WindowManager::initWindow(std::unique_ptr<Window> w, const Style& style) {
  Window& ref = creationTop()->adoptFront(std::move(w));
  ref.init(style);
  creationStack_.push_back(ref.id());
  return ref;
}

void WindowManager::popCreation() {
  if (creationStack_.size() > 1) creationStack_.pop_back();
}

ModalWindow& WindowManager::pushModal(const std::string& name) {
  cancelMouseState();
  modals_.back()->clock.pause(nowMs_);
  auto modal = std::make_unique<ModalWindow>(*this);
  ModalWindow& ref = *modal;
  ref.setName(name);
  ref.setScriptPath(name);
  ref.parent_ = modals_.back().get();
  if (ref.rect().w == 0) ref.setSize(ref.parent_->rect().w, ref.parent_->rect().h);
  modals_.push_back(std::move(modal));
  focus_ = ref.id();
  creationStack_.assign(1, ref.id());
  return ref;
}

std::string WindowManager::idString(WindowId id) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%8x", static_cast<unsigned>(id));
  return buf;
}

void WindowManager::popTop() {
  cancelMouseState();
  std::unique_ptr<ModalWindow> m = std::move(modals_.back());
  modals_.pop_back();
  m->parent_ = nullptr;
  m->setShown(false);
  m->destroyed_ = true;
  destroyed_.push_back(std::move(m));
  focus_ = modals_.back()->id();
  creationStack_.assign(1, focus_);
  modals_.back()->clock.start(nowMs_);
}

bool WindowManager::popModal(const std::string& s) {
  if (s.empty()) return false;
  std::size_t target = 0;  // 0 = no match (the Screen is never popped)
  for (std::size_t i = modals_.size(); i-- > 1;) {
    if (modals_[i]->name() == s || idString(modals_[i]->id()) == s) {
      target = i;
      break;
    }
  }
  if (target == 0) return false;
  while (modals_.size() > target) popTop();
  return true;
}

namespace {

// The clock of the modal that contains `w` (a modal contains itself); the platform time if none is found.
double modalClockNow(const Window& w, double platformNow) {
  for (const Window* p = &w; p; p = p->parent())
    if (p->isModal()) return static_cast<const ModalWindow*>(p)->clock.read(platformNow);
  return platformNow;
}

}  // namespace

double WindowManager::modalNow(const Window& w) const { return modalClockNow(w, nowMs_); }

void WindowManager::startWindowAnimation(WindowId id, double ms, bool catchUp) {
  Window* w = window(id);
  if (!w) return;
  stopWindowAnimation(id);
  WindowAnim a;
  a.id = id;
  a.serial = ++animSerial_;
  a.catchUp = catchUp;
  if (catchUp) a.step.start(modalClockNow(*w, nowMs_), ms);
  else a.ticker.start(modalClockNow(*w, nowMs_), ms);
  anims_.push_back(a);
}

void WindowManager::stopWindowAnimation(WindowId id) {
  std::erase_if(anims_, [id](const WindowAnim& a) { return a.id == id; });
}

void WindowManager::tickWindowAnimations(double platformNowMs) {
  nowMs_ = platformNowMs;
  const std::vector<WindowAnim> snapshot = anims_;
  for (WindowAnim a : snapshot) {
    auto live = [&] {
      return std::find_if(anims_.begin(), anims_.end(), [&](const WindowAnim& x) { return x.serial == a.serial; });
    };
    if (live() == anims_.end()) continue;  // stopped or restarted by an earlier callback
    Window* w = window(a.id);
    if (!w) {
      anims_.erase(live());
      continue;
    }
    if (a.catchUp) {
      // One call per due step. The entry's state is written back before each call, because the callback may stop or
      // restart its own animation (the entry is then gone or has a new serial) or destroy the window.
      for (;;) {
        if (!a.step.due(modalClockNow(*w, platformNowMs))) {
          if (auto it = live(); it != anims_.end()) it->step = a.step;
          break;
        }
        if (auto it = live(); it != anims_.end()) it->step = a.step;
        const bool again = w->onAnimate();
        auto it = live();
        if (it == anims_.end()) break;
        w = window(a.id);
        if (!again || !w) {
          anims_.erase(it);
          break;
        }
      }
      continue;
    }
    if (!a.ticker.due(modalClockNow(*w, platformNowMs))) continue;
    const bool keep = w->onAnimate();  // the window may die in here: not touched again
    auto it = live();
    if (it == anims_.end()) continue;
    if (keep) it->ticker = a.ticker;
    else anims_.erase(it);
  }
}

void WindowManager::clearModalChildren() {
  Window& top = *modals_.back();
  while (!top.children().empty()) destroyWindow(*top.children().front());
  top.setRect({0, 0, 0, 0});
  modals_.back()->awaitingScript = true;
}

void WindowManager::noteScriptLoad(std::string_view name) {
  ModalWindow& m = *modals_.back();
  if (!m.awaitingScript) return;
  m.awaitingScript = false;
  if (name.compare(0, 7, "assets/") == 0) name.remove_prefix(7);
  m.setScriptPath(std::string(name));
}

bool WindowManager::hasScriptError(std::string_view name) const {
  for (auto it = modals_.rbegin(); it != modals_.rend(); ++it)
    if (!name.empty() && ((*it)->scriptPath() == name || (*it)->name() == name)) return (*it)->scriptErrors > 0;
  return false;
}

void WindowManager::destroyWindow(Window& w) {
  if (w.isModal() || w.isDestroyed() || !w.parent()) return;
  Window* p = w.parent();
  std::unique_ptr<Window> owned = p->orphan(w);
  if (!owned) return;
  owned->setShown(false);
  owned->destroyed_ = true;
  destroyed_.push_back(std::move(owned));
}

void WindowManager::clearDestroyed() {
  std::vector<std::unique_ptr<Window>> list;
  list.swap(destroyed_);
  for (auto& w : list) {
    const bool modal = w->isModal();
    const WindowId id = w->id();
    w.reset();
    if (modal) post({kModalClosed, idString(id), 0});
  }
  // Listeners of windows that are gone (a destroyed dialog's DynamicWindow) must not pile up.
  if (!listeners_.empty()) std::erase_if(listeners_, [&](const Listener& l) { return !window(l.owner); });
}

void WindowManager::dispatchMessages() {
  int handled = 0;
  while (!queue_.empty()) {
    if (++handled > kMaxDispatch) {
      overflow_ = true;
      return;
    }
    Message m = std::move(queue_.front());
    queue_.pop_front();
    handle(m);
  }
}

void WindowManager::notifyListeners(const Message& m) {
  // Snapshot: a listener may add listeners or delete windows. Each is checked for life just before it runs.
  std::vector<Listener> snapshot;
  for (const Listener& l : listeners_)
    if (l.type == m.type) snapshot.push_back(l);
  if (snapshot.empty()) return;
  std::erase_if(listeners_, [&](const Listener& l) { return l.type == m.type && !window(l.owner); });
  for (const Listener& l : snapshot)
    if (window(l.owner)) l.fn(m);
}

void WindowManager::handle(const Message& m) {
  if (m.type == kQuitNow) {
    quit_ = true;
    return;
  }
  if (messageHook_ && m.type >= hookMin_ && m.type <= hookMax_ && messageHook_(m)) return;
  if (!listeners_.empty()) notifyListeners(m);
  Window* target = window(m.target);
  if (!target) target = window(focus_);
  if (!target) target = modals_.back().get();
  for (Window* w = target; w; w = w->parent()) {
    if (w->onMessage(m)) return;
    if (w->isModal()) {
      if (auto* modal = static_cast<ModalWindow*>(w)) {
        if (Window* f = window(modal->defaultFocus); f && f != w && f->onMessage(m)) return;
      }
      break;
    }
  }
  if (sink_) sink_->deliver(&m, nullptr);
}

bool WindowManager::labelOf(std::string_view name, std::string& out) const {
  for (auto it = modals_.rbegin(); it != modals_.rend(); ++it) {
    if (const Window* w = (*it)->find(name); w && w->hasLabel()) {
      out = w->label();
      return true;
    }
  }
  return false;
}

bool WindowManager::isShown(std::string_view name) const {
  // Top modal down the chain; a modal matches by its current name or by the script path it was pushed for.
  const Window* w = nullptr;
  for (auto it = modals_.rbegin(); it != modals_.rend() && !w; ++it) {
    if (!name.empty() && (*it)->scriptPath() == name) w = it->get();
    else w = (*it)->find(name);
  }
  if (!w) return false;
  for (; w; w = w->parent())
    if (!w->shown()) return false;
  return true;
}

void WindowManager::draw(gfx::Renderer& renderer) const {
  DrawContext ctx{renderer, {0, 0}, 1.0f};
  for (const auto& m : modals_) drawWindow(*m, ctx);
  if (overlayDrawer_) overlayDrawer_(ctx);
}

namespace {
int countIn(const Window& w, std::string_view prefix) {
  if (!w.shown()) return 0;
  int n = 0;
  if (const sdk::Sprite* root = w.spriteRoot()) n += root->countVisible(prefix);
  for (const auto& c : w.children()) n += countIn(*c, prefix);
  return n;
}

void dumpTree(const Window& w, int depth, std::string& out) {
  out.append(static_cast<std::size_t>(depth) * 2, ' ');
  out += w.describe() + "\n";
  if (const sdk::Sprite* root = w.spriteRoot()) root->dump(out, depth + 1);
  for (const auto& c : w.children()) dumpTree(*c, depth + 1, out);
}
}  // namespace

std::string WindowManager::dump() const {
  std::string out = "[windows] modal depth " + std::to_string(modals_.size()) + ", rects are parent-relative\n";
  for (const auto& m : modals_) dumpTree(*m, 1, out);
  return out;
}

std::string WindowManager::diagnose() const {
  char buf[256];
  std::string out = "modal stack (" + std::to_string(modals_.size()) + ", Screen first, top last):\n";
  for (std::size_t i = 0; i < modals_.size(); ++i) {
    const ModalWindow& m = *modals_[i];
    std::snprintf(buf, sizeof buf, "  [%zu]%s id=%u name=\"%s\" script=\"%s\" shown=%d clock=%s lua-errors=%d\n", i,
                  i + 1 == modals_.size() ? " (top)" : "", static_cast<unsigned>(m.id()), m.name().c_str(),
                  m.scriptPath().c_str(), m.shown() ? 1 : 0, m.clock.paused() ? "paused" : "running", m.scriptErrors);
    out += buf;
  }
  std::snprintf(buf, sizeof buf, "focus id=%u, hover id=%u, destroyed-pending=%zu, quit-requested=%d\n",
                static_cast<unsigned>(focus_), static_cast<unsigned>(hover_), destroyed_.size(), quit_ ? 1 : 0);
  out += buf;
  out += "pending messages (" + std::to_string(queue_.size()) + "):\n";
  for (const Message& m : queue_) {
    std::snprintf(buf, sizeof buf, "  type=%d name=\"%s\" target=%u\n", m.type, m.name.c_str(), static_cast<unsigned>(m.target));
    out += buf;
  }
  out += "transitions running: " + std::to_string(transitions_.size()) + "\n";
  out += "window animations (" + std::to_string(anims_.size()) + "):\n";
  for (const WindowAnim& a : anims_) {
    const Window* w = window(a.id);
    std::snprintf(buf, sizeof buf, "  window id=%u %s name=\"%s\" interval=%g ms%s\n", static_cast<unsigned>(a.id),
                  w ? std::string(w->typeName()).c_str() : "(gone)", w ? w->name().c_str() : "",
                  a.catchUp ? a.step.interval() : a.ticker.interval(), a.catchUp ? " (catch-up)" : "");
    out += buf;
  }
  return out;
}

namespace {
std::optional<gfx::PointF> aimIn(const Window& w) {
  if (!w.shown()) return std::nullopt;
  if (auto p = w.autoAim()) {
    const gfx::PointF o = WindowManager::screenOrigin(w);
    return gfx::PointF{o.x + p->x, o.y + p->y};
  }
  for (const auto& c : w.children())
    if (auto p = aimIn(*c)) return p;
  return std::nullopt;
}
}  // namespace

std::optional<gfx::PointF> WindowManager::autoAim() const { return aimIn(*modals_.back()); }

int WindowManager::countSprites(std::string_view prefix) const {
  int n = 0;
  for (const auto& m : modals_) n += countIn(*m, prefix);
  return n;
}

}  // namespace choco::ui

namespace choco::ui {
void WindowManager::dispatchForeground(bool fg) {
  std::vector<WindowId> ids;
  std::function<void(const Window&)> collect = [&](const Window& w) {
    ids.push_back(w.id());
    for (const auto& c : w.children()) collect(*c);
  };
  for (const auto& m : modals_) collect(*m);
  for (WindowId id : ids)
    if (Window* w = window(id); w && !w->isDestroyed()) w->onForeground(fg);
}
}  // namespace choco::ui
