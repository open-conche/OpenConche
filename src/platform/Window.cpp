#include "platform/Window.h"
#include "app/Diagnostics.h"

#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>

namespace choco::platform {

#ifdef __EMSCRIPTEN__
namespace {
Window* webWindow = nullptr;  // the one window of a web run, for the page's keyboard bridge (choco_web_text below)
}
#endif

struct Window::Impl {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Cursor* cursor = nullptr;  // the live custom cursor, so it outlives SDL_SetCursor
  std::deque<std::string> injectedText;  // text the script driver typed, delivered in order with the other events
  std::deque<std::pair<int, int>> injectedResizes;  // script `resize` requests, applied in order with the other events
};

namespace {
constexpr Sint32 kInjectedTextCode = 1;  // SDL_EVENT_USER code marking one entry of injectedText
constexpr Sint32 kInjectedResizeCode = 2;  // ... and one entry of injectedResizes
}

namespace {

void setColor(SDL_Renderer* r, Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

// Splits "alt+ctrl+Return" into modifiers and a key name.
bool parseKeySpec(const std::string& spec, SDL_Keymod& mod, SDL_Keycode& key, SDL_Scancode& scan) {
  Uint32 bits = 0;
  std::string name = spec;
  for (;;) {
    auto plus = name.find('+');
    if (plus == std::string::npos || plus + 1 >= name.size()) break;
    std::string m = name.substr(0, plus);
    for (auto& ch : m) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (m == "alt") bits |= SDL_KMOD_LALT;
    else if (m == "ctrl" || m == "control") bits |= SDL_KMOD_LCTRL;
    else if (m == "shift") bits |= SDL_KMOD_LSHIFT;
    else if (m == "gui" || m == "cmd") bits |= SDL_KMOD_LGUI;
    else return false;
    name = name.substr(plus + 1);
  }
  mod = static_cast<SDL_Keymod>(bits);
  key = SDL_GetKeyFromName(name.c_str());
  scan = SDL_GetScancodeFromName(name.c_str());
  return key != SDLK_UNKNOWN && scan != SDL_SCANCODE_UNKNOWN;
}

bool inLogicalArea(float x, float y) { return x >= 0 && y >= 0 && x < kLogicalWidth && y < kLogicalHeight; }

}  // namespace

bool isValidKeySpec(const std::string& spec) {
  SDL_Keymod mod;
  SDL_Keycode key;
  SDL_Scancode scan;
  return parseKeySpec(spec, mod, key, scan);
}

Window::Window(Platform& platform, const WindowConfig& config)
    : impl_(std::make_unique<Impl>()),
      headless_(config.headless),
      displayWidth_(config.displayWidth > 0 ? config.displayWidth : 2560),
      displayHeight_(config.displayHeight > 0 ? config.displayHeight : 1440) {
  if (!platform.ok()) {
    error_ = platform.error();
    return;
  }
  scale_ = config.scale < 1 ? 1 : config.scale;
  // Windowed: leave room for the title bar, borders, dock and taskbar before the window exists; a second check
  // with the real border sizes follows creation.
  constexpr int kMarginW = 16, kMarginH = 96;
  SDL_Rect usable{};
  const bool haveUsable = !headless_ && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0;
  if (haveUsable) {
    int fitted = chooseScale(scale_, usable.w - kMarginW, usable.h - kMarginH);
    if (fitted != scale_) {
      diag::err("[display] scale %d does not fit the %dx%d desktop, using scale %d\n", scale_, usable.w,
                   usable.h, fitted);
      scale_ = fitted;
    }
  }

  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;  // the picture is letterboxed to whatever size the user picks
  if (!headless_) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __EMSCRIPTEN__
  flags |= SDL_WINDOW_FILL_DOCUMENT;  // the canvas is the whole page and follows the browser window (cmake/Web.cmake)
#endif
  if (config.fullscreen && !headless_) flags |= SDL_WINDOW_FULLSCREEN;  // no mode set: borderless desktop fullscreen

  impl_->window = SDL_CreateWindow(config.title.c_str(), kLogicalWidth * scale_, kLogicalHeight * scale_, flags);
  if (!impl_->window) {
    error_ = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
    return;
  }
  SDL_SetWindowMinimumSize(impl_->window, kLogicalWidth, kLogicalHeight);
  if (haveUsable && !config.fullscreen) {
    int top = 0, left = 0, bottom = 0, right = 0;
    if (SDL_GetWindowBordersSize(impl_->window, &top, &left, &bottom, &right)) {
      while (scale_ > 1 && (kLogicalWidth * scale_ + left + right > usable.w ||
                            kLogicalHeight * scale_ + top + bottom > usable.h)) {
        --scale_;
        diag::err("[display] window with frame does not fit the desktop, using scale %d\n", scale_);
        SDL_SetWindowSize(impl_->window, kLogicalWidth * scale_, kLogicalHeight * scale_);
      }
    }
  }
  impl_->renderer = SDL_CreateRenderer(impl_->window, headless_ ? "software" : nullptr);
  if (!impl_->renderer) {
    error_ = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
    return;
  }
  SDL_SetRenderLogicalPresentation(impl_->renderer, kLogicalWidth, kLogicalHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  windowedW_ = kLogicalWidth * scale_;
  windowedH_ = kLogicalHeight * scale_;
  if (!headless_) SDL_SetRenderVSync(impl_->renderer, 1);
  else if (config.fullscreen) applyFullscreen(true);
  reportedFullscreen_ = isFullscreen();  // what SDL accepted, not what was asked
  outputSize(reportedW_, reportedH_);  // the size at startup is not news
  ok_ = true;
#ifdef __EMSCRIPTEN__
  webWindow = this;
#endif
}

void Window::setTextInput(bool on, float lx, float ly, float lw, float lh) {
  if (!impl_->window) return;
  if (on) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    SDL_RenderCoordinatesToWindow(impl_->renderer, lx, ly, &x0, &y0);
    SDL_RenderCoordinatesToWindow(impl_->renderer, lx + lw, ly + lh, &x1, &y1);
    const SDL_Rect area{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
    SDL_SetTextInputArea(impl_->window, &area, 0);
    if (!textInput_) SDL_StartTextInput(impl_->window);  // typed characters arrive as SDL_EVENT_TEXT_INPUT
  } else if (textInput_) {
    SDL_StopTextInput(impl_->window);
  }
#ifdef __EMSCRIPTEN__
  // SDL's web backend has no on-screen keyboard: the page raises one (packaging/web/index.html's chocoTextInput)
  // and feeds what is typed back through choco_web_text / choco_web_key below.
  if (on != textInput_) EM_ASM({ chocoTextInput($0); }, on ? 1 : 0);
#endif
  textInput_ = on;
}

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE void choco_web_text(const char* utf8) {
  if (webWindow) webWindow->injectText(utf8);
}
EMSCRIPTEN_KEEPALIVE void choco_web_key(const char* spec) {
  if (webWindow && !webWindow->injectKey(spec)) diag::out("[input] web keyboard: unknown key '%s'\n", spec);
}
}
#endif

Window::~Window() {
#ifdef __EMSCRIPTEN__
  if (webWindow == this) webWindow = nullptr;
#endif
  if (impl_->cursor) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_DestroyCursor(impl_->cursor);
  }
  if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
}

bool Window::setCursor(const gfx::ImageData& image, int hotspotX, int hotspotY) {
  if (impl_->cursor) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_DestroyCursor(impl_->cursor);
    impl_->cursor = nullptr;
  }
  if (image.width <= 0 || image.height <= 0 || image.rgba.size() < static_cast<std::size_t>(image.width) * image.height * 4)
    return false;
  // gfx::ImageData is straight-alpha RGBA8, the layout SDL_CreateColorCursor takes (ABGR little-endian).
  SDL_Surface* surface = SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_ABGR8888);
  if (!surface) return false;
  std::memcpy(surface->pixels, image.rgba.data(), static_cast<std::size_t>(surface->pitch) * image.height);
  SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotspotX, hotspotY);
  SDL_DestroySurface(surface);
  if (!cursor) return false;  // headless dummy video: no cursor support
  SDL_SetCursor(cursor);
  impl_->cursor = cursor;
  return true;
}

void Window::clearCursor() {
  if (!impl_->cursor) return;
  SDL_SetCursor(SDL_GetDefaultCursor());
  SDL_DestroyCursor(impl_->cursor);
  impl_->cursor = nullptr;
}

bool Window::cursorActive() const { return impl_->cursor != nullptr; }

bool Window::applyCursorColourKey(gfx::ImageData& img) {
  bool keyed = false;
  const std::size_t n = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
  if (img.rgba.size() < n * 4) return false;
  for (std::size_t i = 0; i < n; ++i) {
    std::uint8_t* px = img.rgba.data() + i * 4;
    if (px[0] == 255 && px[1] == 0 && px[2] == 255) {
      px[3] = 0;
      keyed = true;
    }
  }
  return keyed;
}

void* Window::rendererHandle() const { return impl_->renderer; }

bool Window::outputSize(int& w, int& h) const { return SDL_GetWindowSizeInPixels(impl_->window, &w, &h); }

bool Window::isFullscreen() const {
  if (headless_) return emulatedFullscreen_;
  return (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::refreshPresentation() {
  SDL_SetRenderLogicalPresentation(impl_->renderer, kLogicalWidth, kLogicalHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

// The dummy driver has no desktop, so fullscreen is emulated: the window takes the size of the emulated display
// and the same logical letterbox presentation applies as on a real one.
bool Window::applyFullscreen(bool on) {
  if (!headless_) {
    if (!SDL_SetWindowFullscreen(impl_->window, on)) return false;
    // SDL_SyncWindow waits for the desktop to finish the transition (a Space on macOS). The desktop can refuse it
    // (the Cocoa path reports windowDidFailToEnterFullScreen and SDL clears the flag), so the flag after the wait is
    // the answer, not the request: a refusal is a failed switch and the state stays as it was.
    SDL_SyncWindow(impl_->window);
    if (isFullscreen() != on) {
      diag::err("[display] fullscreen %s refused by the desktop\n", on ? "on" : "off");
      return false;
    }
    reportedFullscreen_ = on;  // the ENTER/LEAVE event this causes is not news
    return true;
  }
  emulatedFullscreen_ = on;
  reportedFullscreen_ = on;
  // Leaving returns to the windowed size the window had (a real window gets this from SDL itself).
  int w = on ? displayWidth_ : windowedW_;
  int h = on ? displayHeight_ : windowedH_;
  SDL_SetWindowSize(impl_->window, w, h);
  SDL_SyncWindow(impl_->window);
  refreshPresentation();
  return true;
}

bool Window::toggleFullscreen() { return applyFullscreen(!isFullscreen()); }

std::vector<InputEvent> Window::pollEvents() {
  std::vector<InputEvent> out;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        InputEvent e;
        e.type = InputEvent::Type::Quit;
        out.push_back(e);
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        SDL_ConvertEventToRenderCoordinates(impl_->renderer, &ev);
        InputEvent e;
        if (ev.type == SDL_EVENT_MOUSE_MOTION) {
          e.type = InputEvent::Type::MouseMove;
          e.x = ev.motion.x;
          e.y = ev.motion.y;
        } else {
          e.type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEvent::Type::MouseDown : InputEvent::Type::MouseUp;
          e.x = ev.button.x;
          e.y = ev.button.y;
          e.button = ev.button.button;
        }
        e.inArea = inLogicalArea(e.x, e.y);
        out.push_back(e);
        break;
      }
      case SDL_EVENT_KEY_DOWN: {
        bool alt = (ev.key.mod & SDL_KMOD_ALT) != 0;
        if (!ev.key.repeat &&
            (ev.key.key == SDLK_F11 || (alt && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER)))) {
          if (!toggleFullscreen()) break;  // refused: nothing changed, nothing to report
          InputEvent e;
          e.type = InputEvent::Type::FullscreenChanged;
          e.fullscreen = reportedFullscreen_;
          int ow = 0, oh = 0;
          if (headless_ && outputSize(ow, oh)) {
            e.x = static_cast<float>(ow);
            e.y = static_cast<float>(oh);
            reportedW_ = ow;  // this event carries the new size: no separate Resized for it
            reportedH_ = oh;
          }
          out.push_back(e);
          break;
        }
        InputEvent e;
        e.type = InputEvent::Type::Key;
        e.key = SDL_GetKeyName(ev.key.key);
        e.repeat = ev.key.repeat;
        e.alt = alt;
        e.ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;
        e.shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
        e.gui = (ev.key.mod & SDL_KMOD_GUI) != 0;  // Cmd on macOS: switcher shortcuts take it too
        out.push_back(e);
        break;
      }
      case SDL_EVENT_TEXT_INPUT: {
        InputEvent e;
        e.type = InputEvent::Type::Text;
        e.key = ev.text.text ? ev.text.text : "";
        if (!e.key.empty()) out.push_back(e);
        break;
      }
      case SDL_EVENT_USER:
        if (ev.user.code == kInjectedResizeCode && !impl_->injectedResizes.empty()) {
          // Applied here, in order: mouse events queued before it were converted with the old size.
          const auto [w, h] = impl_->injectedResizes.front();
          impl_->injectedResizes.pop_front();
          applyWindowSize(w, h);
        } else if (ev.user.code == kInjectedTextCode && !impl_->injectedText.empty()) {
          InputEvent e;
          e.type = InputEvent::Type::Text;
          e.key = std::move(impl_->injectedText.front());
          impl_->injectedText.pop_front();
          out.push_back(std::move(e));
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_DID_ENTER_BACKGROUND:
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_DID_ENTER_FOREGROUND: {
        InputEvent e;
        e.type = InputEvent::Type::Foreground;
        e.foreground = ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED || ev.type == SDL_EVENT_WINDOW_RESTORED ||
                       ev.type == SDL_EVENT_DID_ENTER_FOREGROUND;
        out.push_back(e);
        break;
      }
      case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
      case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        // The OS changed the state (green button, window menu); our own toggles already set reportedFullscreen_.
        const bool on = ev.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        if (on != reportedFullscreen_) {
          reportedFullscreen_ = on;
          InputEvent e;
          e.type = InputEvent::Type::FullscreenChanged;
          e.fullscreen = on;
          out.push_back(e);
        }
        break;
      }
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        refreshPresentation();  // keeps the logical presentation in force on every backend
        sizeEventSeen_ = true;
        break;
      case SDL_EVENT_RENDER_DEVICE_LOST:
        diag::err("[render] device lost\n");
        break;
      case SDL_EVENT_RENDER_TARGETS_RESET:
      case SDL_EVENT_RENDER_DEVICE_RESET: {
        // The App rebuilds GPU textures when a "device" reset arrives.
        InputEvent e;
        e.type = InputEvent::Type::RenderReset;
        e.key = ev.type == SDL_EVENT_RENDER_TARGETS_RESET ? "targets" : "device";
        out.push_back(e);
        break;
      }
      default:
        break;
    }
  }
  if (sizeEventSeen_) {
    // One Resized for however many size events came in, and none if the size ended where the app already knew it.
    sizeEventSeen_ = false;
    int ow = 0, oh = 0;
    if (outputSize(ow, oh) && (ow != reportedW_ || oh != reportedH_)) {
      reportedW_ = ow;
      reportedH_ = oh;
      InputEvent e;
      e.type = InputEvent::Type::Resized;
      e.x = static_cast<float>(ow);
      e.y = static_cast<float>(oh);
      out.push_back(e);
    }
  }
  deliverFolderPick();  // a completed pickFolder lands on this thread, among the events
  return out;
}

void Window::folderPickCallback(void* userdata, const char* const* files, int filter) {
  (void)filter;
  // SDL calls this exactly once per dialog (also on error), so the weak pointer it carries is deleted here. A
  // pick that was cancelled or whose Window is gone finds the state expired and writes nothing.
  auto* weak = static_cast<std::weak_ptr<FolderPickState>*>(userdata);
  if (const std::shared_ptr<FolderPickState> state = weak->lock()) {
    std::lock_guard<std::mutex> lock(state->mutex);
    std::optional<std::string> folder;
    if (files && files != (const char* const*)-1 && files[0]) folder = files[0];
    // A null list is SDL's error report (a cancel is an empty list): keep its reason for the caller.
    if (!files) state->error = SDL_GetError();
    state->result = folder;  // engaged outer = the pick happened (inner empty = cancelled or failed)
  }
  delete weak;
}

void Window::pickFolder(std::function<void(const std::optional<std::string>&)> onDone) {
  folderOnDone_ = std::move(onDone);
  folderPickError_.clear();
  folderState_ = std::make_shared<FolderPickState>();  // a previous pick still open now writes into nothing
  // Unsupported backends (headless dummy video, a headless Unix session) invoke the callback with an error, so
  // onDone still fires with nullopt.
  SDL_ShowOpenFolderDialog(&Window::folderPickCallback, new std::weak_ptr<FolderPickState>(folderState_),
                           impl_->window, nullptr, false);
}

void Window::cancelFolderPick() {
  folderOnDone_ = nullptr;
  folderState_.reset();
}

void Window::deliverFolderPick() {
  if (!folderState_) return;
  std::optional<std::optional<std::string>> result;
  {
    std::lock_guard<std::mutex> lock(folderState_->mutex);
    if (!folderState_->result) return;
    result = folderState_->result;
    folderPickError_ = folderState_->error;
    folderState_->result.reset();
  }
  if (folderOnDone_) folderOnDone_(*result);
}

void Window::beginFrame() {
  flushDraws();
  // With letterbox presentation SDL_RenderClear clears the whole output, bars included.
  SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
  SDL_RenderClear(impl_->renderer);
}

void Window::clear(Color c) { fillRect({0, 0, kLogicalWidth, kLogicalHeight}, c); }

void Window::fillRect(Rect r, Color c) {
  flushDraws();
  setColor(impl_->renderer, c);
  SDL_FRect f{r.x, r.y, r.w, r.h};
  SDL_RenderFillRect(impl_->renderer, &f);
}

void Window::outlineRect(Rect r, Color c) {
  flushDraws();
  setColor(impl_->renderer, c);
  SDL_FRect f{r.x, r.y, r.w, r.h};
  SDL_RenderRect(impl_->renderer, &f);
}

void Window::endFrame() { flushDraws(); }

void Window::present() {
  flushDraws();
  SDL_RenderPresent(impl_->renderer);
}

bool Window::readPixels(Image& out) { return readRegion(out, false); }

bool Window::readLogicalPixels(Image& out) { return readRegion(out, true); }

bool Window::readRegion(Image& out, bool logicalOnly) {
  flushDraws();
  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  // With a logical presentation active a null rect means the logical viewport only, so lift it to read the whole
  // output (letterbox bars included) and put it back afterwards. Reading is a debug path, not per-frame.
  SDL_GetRenderLogicalPresentation(impl_->renderer, &lw, &lh, &mode);
  SDL_SetRenderLogicalPresentation(impl_->renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_Surface* raw = SDL_RenderReadPixels(impl_->renderer, nullptr);
  SDL_SetRenderLogicalPresentation(impl_->renderer, lw, lh, mode);
  if (!raw) {
    error_ = std::string("SDL_RenderReadPixels failed: ") + SDL_GetError();
    return false;
  }
  SDL_Surface* conv = raw->format == SDL_PIXELFORMAT_RGBA32 ? raw : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
  if (conv != raw) SDL_DestroySurface(raw);
  if (!conv) {
    error_ = std::string("SDL_ConvertSurface failed: ") + SDL_GetError();
    return false;
  }
  const auto* src = static_cast<const std::uint8_t*>(conv->pixels);
  const std::size_t pitch = static_cast<std::size_t>(conv->pitch);
  int x0 = 0, y0 = 0, cw = conv->w, ch = conv->h;
  if (logicalOnly) {
    SDL_FRect r{};
    if (SDL_GetRenderLogicalPresentationRect(impl_->renderer, &r)) {
      x0 = std::max(0, static_cast<int>(std::lround(r.x)));
      y0 = std::max(0, static_cast<int>(std::lround(r.y)));
      cw = std::min(conv->w - x0, static_cast<int>(std::lround(r.w)));
      ch = std::min(conv->h - y0, static_cast<int>(std::lround(r.h)));
    }
  }
  if (cw <= 0 || ch <= 0) {
    SDL_DestroySurface(conv);
    error_ = "empty capture area";
    return false;
  }
  if (!logicalOnly || (cw == kLogicalWidth && ch == kLogicalHeight)) {
    out.width = cw;
    out.height = ch;
    out.rgba.resize(static_cast<std::size_t>(cw) * ch * 4);
    for (int y = 0; y < ch; ++y) {
      std::memcpy(out.rgba.data() + static_cast<std::size_t>(y) * cw * 4,
                  src + (static_cast<std::size_t>(y0) + y) * pitch + static_cast<std::size_t>(x0) * 4,
                  static_cast<std::size_t>(cw) * 4);
    }
  } else {
    // Box filter down (or up) to 800x600.
    out.width = kLogicalWidth;
    out.height = kLogicalHeight;
    out.rgba.assign(static_cast<std::size_t>(kLogicalWidth) * kLogicalHeight * 4, 0);
    for (int y = 0; y < kLogicalHeight; ++y) {
      int sy0 = y * ch / kLogicalHeight;
      int sy1 = std::max(sy0 + 1, (y + 1) * ch / kLogicalHeight);
      for (int x = 0; x < kLogicalWidth; ++x) {
        int sx0 = x * cw / kLogicalWidth;
        int sx1 = std::max(sx0 + 1, (x + 1) * cw / kLogicalWidth);
        unsigned sum[4] = {0, 0, 0, 0};
        for (int sy = sy0; sy < sy1; ++sy) {
          for (int sx = sx0; sx < sx1; ++sx) {
            const std::uint8_t* p = src + (static_cast<std::size_t>(y0) + sy) * pitch + (static_cast<std::size_t>(x0) + sx) * 4;
            for (int k = 0; k < 4; ++k) sum[k] += p[k];
          }
        }
        unsigned n = static_cast<unsigned>((sy1 - sy0) * (sx1 - sx0));
        std::uint8_t* d = out.rgba.data() + (static_cast<std::size_t>(y) * kLogicalWidth + x) * 4;
        for (int k = 0; k < 4; ++k) d[k] = static_cast<std::uint8_t>((sum[k] + n / 2) / n);
      }
    }
  }
  SDL_DestroySurface(conv);
  return true;
}

void Window::injectMouseMove(float lx, float ly) {
  float wx = 0, wy = 0;
  SDL_RenderCoordinatesToWindow(impl_->renderer, lx, ly, &wx, &wy);
  injectWindowMouseMove(wx, wy);
}

void Window::injectMouseButton(float lx, float ly, bool down) {
  float wx = 0, wy = 0;
  SDL_RenderCoordinatesToWindow(impl_->renderer, lx, ly, &wx, &wy);
  injectWindowMouseButton(wx, wy, down);
}

void Window::injectWindowMouseMove(float wx, float wy) {
  SDL_Event ev{};
  ev.type = SDL_EVENT_MOUSE_MOTION;
  ev.motion.windowID = SDL_GetWindowID(impl_->window);
  ev.motion.which = 0;
  ev.motion.x = wx;
  ev.motion.y = wy;
  SDL_PushEvent(&ev);
}

void Window::injectWindowMouseButton(float wx, float wy, bool down) {
  SDL_Event ev{};
  ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  ev.button.windowID = SDL_GetWindowID(impl_->window);
  ev.button.which = 0;
  ev.button.button = SDL_BUTTON_LEFT;
  ev.button.down = down;
  ev.button.clicks = 1;
  ev.button.x = wx;
  ev.button.y = wy;
  SDL_PushEvent(&ev);
}

bool Window::injectKey(const std::string& spec) {
  SDL_Keymod mod;
  SDL_Keycode key;
  SDL_Scancode scan;
  if (!parseKeySpec(spec, mod, key, scan)) return false;
  for (bool down : {true, false}) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.windowID = SDL_GetWindowID(impl_->window);
    ev.key.which = 0;
    ev.key.key = key;
    ev.key.scancode = scan;
    ev.key.mod = mod;
    ev.key.down = down;
    SDL_PushEvent(&ev);
  }
  // A real keyboard also produces text for a printable key; the injected key does it here (not with ctrl, alt or gui).
  if (!(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
    if (key == SDLK_SPACE) {
      injectText(" ");
    } else if (key > 0x20 && key < 0x7f) {
      char c = static_cast<char>(key);
      if ((mod & SDL_KMOD_SHIFT) && std::isalpha(static_cast<unsigned char>(c))) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      injectText(std::string(1, c));
    }
  }
  return true;
}

void Window::injectText(const std::string& utf8) {
  if (utf8.empty()) return;
  impl_->injectedText.push_back(utf8);
  SDL_Event ev{};
  ev.type = SDL_EVENT_USER;
  ev.user.windowID = SDL_GetWindowID(impl_->window);
  ev.user.code = kInjectedTextCode;
  SDL_PushEvent(&ev);
}

void Window::injectForeground(bool fg) {
  SDL_Event ev{};
  ev.type = fg ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
  ev.window.windowID = SDL_GetWindowID(impl_->window);
  SDL_PushEvent(&ev);
}

void Window::injectResize(int w, int h) {
  if (w < 1 || h < 1) return;
  impl_->injectedResizes.emplace_back(w, h);
  SDL_Event ev{};
  ev.type = SDL_EVENT_USER;
  ev.user.windowID = SDL_GetWindowID(impl_->window);
  ev.user.code = kInjectedResizeCode;
  SDL_PushEvent(&ev);
}

void Window::applyWindowSize(int w, int h) {
  SDL_SetWindowSize(impl_->window, w, h);
  SDL_SyncWindow(impl_->window);
  refreshPresentation();
  // The dummy driver may not deliver a size event; the next poll compares the size either way, so a Resized is
  // reported once whether or not SDL sent one.
  sizeEventSeen_ = true;
  if (headless_ && !emulatedFullscreen_) {
    windowedW_ = w;
    windowedH_ = h;
  }
}

void Window::injectQuit() {
  SDL_Event ev{};
  ev.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&ev);
}

}  // namespace choco::platform
