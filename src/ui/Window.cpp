#include "ui/Window.h"

#include <algorithm>

#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::ui {

namespace {

bool labelType(const std::string& type) {
  return type == "Text" || type == "TightText" || type == "TextEdit" || type == "Button";
}

}  // namespace

Window::Window(WindowManager& wm) : wm_(wm), id_(wm.nextId()) { wm_.registerWindow(this); }

Window::~Window() { wm_.unregisterWindow(this); }

Window& Window::adoptFront(std::unique_ptr<Window> child) {
  Window& ref = *child;
  child->parent_ = this;
  children_.insert(children_.begin(), std::move(child));
  return ref;
}

std::unique_ptr<Window> Window::orphan(Window& child) {
  auto it = std::find_if(children_.begin(), children_.end(), [&](const auto& c) { return c.get() == &child; });
  if (it == children_.end()) return nullptr;
  std::unique_ptr<Window> out = std::move(*it);
  children_.erase(it);
  out->parent_ = nullptr;
  return out;
}

Window* Window::find(std::string_view name) {
  if (name.empty()) return nullptr;
  if (name_ == name) return this;
  for (auto& c : children_)
    if (Window* w = c->find(name)) return w;
  return nullptr;
}

const Window* Window::find(std::string_view name) const { return const_cast<Window*>(this)->find(name); }

void Window::sendToBack() {
  if (!parent_) return;
  auto& list = parent_->children_;
  for (auto it = list.begin(); it != list.end(); ++it)
    if (it->get() == this) {
      std::rotate(it, it + 1, list.end());
      return;
    }
}

void Window::bringToFront() {
  if (!parent_) return;
  auto& list = parent_->children_;
  for (auto it = list.begin(); it != list.end(); ++it)
    if (it->get() == this) {
      std::rotate(list.begin(), it, it + 1);
      return;
    }
}

void Window::init(const Style& style) {
  if (auto n = style.string("name")) name_ = *n;
  sizeAndPositionFromStyle(*this, style);
}

void Window::postChildrenInit(const Style& style) {
  if (style.booleanOr("fit", false)) fitToChildren(*this);
  sizeAndPositionFromStyle(*this, style);
}

const std::string& Window::label() const {
  static const std::string empty;
  return empty;
}

std::string Window::describe() const {
  std::string s(typeName());
  s += " \"" + name_ + "\" id=" + std::to_string(id_) + " rect=" + std::to_string(rect_.x) + "," +
       std::to_string(rect_.y) + "," + std::to_string(rect_.w) + "," + std::to_string(rect_.h);
  if (!shown()) s += " hidden";
  if (hasLabel()) s += " label=\"" + label() + "\"";
  return s + detail();
}

PlaceholderWindow::PlaceholderWindow(WindowManager& wm, std::string typeName)
    : Window(wm), typeName_(std::move(typeName)), hasLabel_(labelType(typeName_)) {}

void PlaceholderWindow::init(const Style& style) {
  Window::init(style);
  if (auto l = style.string("label")) label_ = *l;
}

ModalWindow::ModalWindow(WindowManager& wm) : Window(wm) { setFlags(flags() | kModalFlag | kFocusableFlag); }

void ModalWindow::postChildrenInit(const Style& style) {
  fitToChildren(*this);
  Window::postChildrenInit(style);
}

bool ModalWindow::onMessage(const Message& m) {
  if (m.type != kCloseWindow) return false;
  if (!m.name.empty()) manager().popModal(m.name);
  return true;
}

void ModalWindow::addHotkey(const std::string& key, const std::string& button) {
  const std::string k = normalizeKeyString(key);
  if (button.empty()) hotkeys_.erase(k);
  else hotkeys_[k] = button;
}

namespace {

bool visibleToModal(const Window* w) {
  for (; w; w = w->parent()) {
    if (!w->shown()) return false;
    if (w->isModal()) break;
  }
  return true;
}

// FindDefault::Process, driven by TWindow::ForEachChild: forward over each child list, pre-order, front child first.
// Every enabled `default` or `cancel` button replaces the earlier one of its kind, so the last match wins. A button
// that leaves both slots filled ends only the child list it sits in (the callback returns 0 for that list); container
// nodes always continue, so the outer lists keep walking and a later button there still overwrites.
struct DefaultCancel {
  ButtonWindow* def = nullptr;
  ButtonWindow* cancel = nullptr;
};

// Returns false when the sibling list being walked should stop.
bool findDefaultCancel(Window& w, DefaultCancel& out) {
  auto* b = dynamic_cast<ButtonWindow*>(&w);
  if (b) {
    if (visibleToModal(b)) {
      if (b->isDefault()) out.def = b;
      if (b->isCancel()) out.cancel = b;
    }
    if (out.def && out.cancel) return false;
  }
  for (const auto& c : w.children())
    if (!findDefaultCancel(*c, out)) break;
  return true;
}

}  // namespace

bool ModalWindow::onKeyDown(const KeyEvent& ev) {
  if (hotkeys_.empty()) return false;
  auto it = hotkeys_.find(normalizeKeyString(hotkeyOf(ev, true)));
  if (it == hotkeys_.end()) it = hotkeys_.find(normalizeKeyString(hotkeyOf(ev, false)));  // retry with the bare key
  if (it == hotkeys_.end()) return false;
  const std::string name = it->second;
  Window* w = find(name);
  auto* b = dynamic_cast<ButtonWindow*>(w);
  if (!b) {
    manager().log("ERROR: Hot key button not found. Button:'" + name + "'");
    return true;
  }
  b->press();  // TModalWindow::PressButton presses hidden buttons too
  return true;
}

// Enter presses the `default` button's command and Escape the `cancel` button's; the modal then sends kDefaultAction to
// the focus window unless the button was detached by its command. The event is always consumed.
bool ModalWindow::onChar(const KeyEvent& ev) {
  const char c = keyChar(ev);
  if (c != '\r' && c != 27) return false;
  DefaultCancel found;
  findDefaultCancel(*this, found);
  if (manager().legacyCancelDefault() && found.def && found.def->labelKey() == "cancel") {
    // Chocolatier 2 buysell.lua: the Cancel button carries `default`. It answers Escape; Enter goes to its OK sibling.
    ButtonWindow* cancelBtn = found.def;
    if (!found.cancel) found.cancel = cancelBtn;
    found.def = nullptr;
    if (Window* p = cancelBtn->parent())
      for (const auto& ch : p->children()) {
        auto* sib = dynamic_cast<ButtonWindow*>(ch.get());
        if (sib && sib != cancelBtn && visibleToModal(sib)) {
          found.def = sib;
          break;
        }
      }
  }
  ButtonWindow* b = c == '\r' ? found.def : found.cancel;
  // TModalWindow::OnChar: kDefaultAction goes to the focus window at once (not queued) unless the pressed button's
  // command detached it.
  if (!b || !b->doCommand()) manager().sendMessage({kDefaultAction, "", manager().focus()});
  return true;
}

void ModalWindow::onSetFocus(WindowId) {
  if (Window* f = manager().window(defaultFocus); f && f != this) manager().setFocus(f->id());
}

Screen::Screen(WindowManager& wm) : ModalWindow(wm) { setRect({0, 0, 800, 600}); }

}  // namespace choco::ui
