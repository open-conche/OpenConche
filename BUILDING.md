# Building OpenConche

## Requirements

- C++20, CMake + Ninja, and network on first configure (SDL 3.x, Lua 5.0.2 and doctest are fetched).
- Your own copies of the games to run it (see the README).

## Build and test

    cmake -S . -B build -G Ninja && cmake --build build
    ctest --test-dir build -j4                      # the full suite
    ./build/engine_tests                             # the unit tests alone
    cmake --build build --target openconche          # the game binary

Build options: `CHOCO_GAME_CHOC1` / `CHOCO_GAME_CHOC2` (default ON) build the per-game modules.

### Release archives

    cmake -S . -B build/release -G Ninja -DCHOCO_RELEASE=ON
    cmake --build build/release --target dist

`CHOCO_RELEASE` is the shipping configuration: a Release build, an arm64-only
`OpenConche.app` on macOS (ad-hoc signed only, so a downloaded copy needs
`xattr -dr com.apple.quarantine OpenConche.app` once before Gatekeeper lets it
open), a static C runtime on Windows, static libstdc++ on Linux. The `dist`
target zips the executable with `LICENSE.txt`, `THIRD_PARTY.txt` and a short
`README.txt` into `build/release/dist/openconche-<version>-<os>-<arch>.zip`
(`.tar.gz` on Linux; build it on the oldest glibc you mean to support, the
binary runs on anything newer); `openconche --version` prints the version,
which is set once in `CMakeLists.txt` (`project(... VERSION ...)`). On Windows
use the Visual Studio generator (`-A x64`, then `--config Release`).

On Linux install SDL's window and audio backends before configuring, or the
binary won't have a display driver (Debian and Ubuntu names): `libx11-dev
libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev
libxtst-dev libwayland-dev libxkbcommon-dev libdecor-0-dev libegl1-mesa-dev
libgl1-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev
libpipewire-0.3-dev libdbus-1-dev libudev-dev`. They are build-time only: the
shipped binary loads them at run time if present.

The engine also builds for the browser with Emscripten (`cmake/Web.cmake`:
`emcmake cmake ... -DCHOCO_WEB_GAMES="choc3=<assets dir>"`, then the `web`
target). The result is a static site with the game data packaged in; saves live
in the browser's IndexedDB. It is meant for your own copies on your own network. What you do with it is up
to you, but this is legal territory: don't put the game data on a public site
unless you have permission to distribute the original assets. The reason for
this support is to make iOS and Android usable without native apps for them.

## Testing and the headless harness

Automated runs never touch a real profile: every scripting flag requires
`--userdir` pointing outside the default folders. The harness drives the real
games end-to-end — `build/openconche --headless --assets ... --userdir
<scratch> --script x.script` — with commands like `click`, `type`, `key`,
`lua`, `expect-window`, `dump` and `screenshot-logical`.

    ctest --test-dir build -R '^(choc1_story|choc2_story|reforged_endtoend|vanilla_endtoend)$'

The operator's manual, `AGENTS.md`, has every flag and the play-testing workflow.

## Layout

```
src/            the engine: {platform,gfx,audio,assets,ui,sdk,script,app} + games/{common,choc3,choc2,choc1}
lua/            prelude, host and animation script, embedded at build time
tools/          gentools and checks: gen_stubs, gen_audio_events, check_layering, compare_test_out, playtest/ (--run snippets)
tests/          the unit tests and the check_*.py end-to-end scenarios, with the fixture trees and the boot goldens
docs/           design/ (the design docs), re/ (the RE fact files)
third_party/    stb_image, stb_vorbis (public domain)
AGENTS.md       the operator's manual: flags, the script harness, play-testing, diagnosing a hang
```
