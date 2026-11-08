#include "platform/Platform.h"

#include <SDL3/SDL.h>

namespace choco::platform {

Platform::Platform(bool headless) : headless_(headless) {
  if (headless_) {
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
  }
  // Closing the last window must yield one close request (WINDOW_CLOSE_REQUESTED) only; SDL's default also posts
  // SDL_EVENT_QUIT, which the app would read as a second request. Cmd-Q / Alt-F4 / SIGTERM still produce SDL_EVENT_QUIT.
  SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    error_ = std::string("SDL_Init failed: ") + SDL_GetError();
    return;
  }
  ok_ = true;
}

Platform::~Platform() {
  if (ok_) SDL_Quit();
}

void showWarning(const std::string& title, const std::string& text) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title.c_str(), text.c_str(), nullptr);
}

}  // namespace choco::platform
