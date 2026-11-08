#pragma once

#include <string>

namespace choco::platform {

// SDL initialisation and shutdown. Create this before any Window and destroy it after the last one.
// In headless mode it forces the dummy video driver, even if SDL_VIDEO_DRIVER is exported.
class Platform {
 public:
  explicit Platform(bool headless);
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  bool headless() const { return headless_; }

 private:
  bool ok_ = false;
  bool headless_ = false;
  std::string error_;
};

// A modal warning box (works before the window exists). Best effort: returns quietly if none can be shown.
void showWarning(const std::string& title, const std::string& text);

}  // namespace choco::platform
