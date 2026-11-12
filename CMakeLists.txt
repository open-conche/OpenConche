cmake_minimum_required(VERSION 3.24)

# CHOCO_RELEASE=ON is the shipping configuration (cmake/Distribution.cmake): Release build, arm64-only macOS app bundle,
# static MSVC runtime, portable Linux binary, and a `dist` target that zips the result. Off is the everyday build.
option(CHOCO_RELEASE "Configure for a distributable build and enable the dist target" OFF)
if(CHOCO_RELEASE)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  if(APPLE)
    # Apple silicon only: no universal binaries.
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "" FORCE)
  endif()
  # Windows: the C runtime goes into the exe, so the zip needs no redistributable (SDL has its own switch for the same).
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)
endif()

project(chocolatier_engine VERSION 1.1 LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)  # the executable icon, packaging/icon/openconche.rc.in
endif()
if(CHOCO_RELEASE AND NOT MSVC)
  # No build-machine paths in the shipped binary (__FILE__ in SDL's and the engine's assert strings).
  add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CHOCO_WARNINGS_AS_ERRORS "Treat warnings in engine code as errors" ON)

# Debug builds run under AddressSanitizer and UBSan (engine and fetched libraries alike); other build types are untouched.
if(NOT MSVC AND NOT EMSCRIPTEN)
  option(CHOCO_SANITIZE "AddressSanitizer and UBSan in Debug builds" ON)
  if(CHOCO_SANITIZE)
    add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined>" "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>")
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
  endif()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- SDL3 (pinned, static) ----
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Configure even without X11/Wayland dev packages (headless runs use the dummy driver).
  set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-3.4.18
  GIT_SHALLOW    TRUE)

# ---- Lua 5.0.2 (sha256 recorded here) ----
FetchContent_Declare(lua502
  URL      https://www.lua.org/ftp/lua-5.0.2.tar.gz
  URL_HASH SHA256=a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- doctest ----
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.12
  GIT_SHALLOW    TRUE)

# ---- pugixml (pinned) ----
FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 lua502 doctest pugixml)

# SDL's Cocoa/Metal sources redeclare typedefs (legal C11, noisy under clang's default C standard).
if(APPLE AND TARGET SDL3-static)
  target_compile_options(SDL3-static PRIVATE -Wno-typedef-redefinition)
endif()

# The game's Lua has a `continue` statement that stock 5.0.2 lacks (Reforged's sim/port.lua uses it).
include(cmake/PatchLua.cmake)
patch_lua("${lua502_SOURCE_DIR}")

# Lua 5.0.2 as a C static library: core in src/, libraries in src/lib/ (which includes lauxlib.c).
file(GLOB LUA_CORE_SRC "${lua502_SOURCE_DIR}/src/*.c")
list(FILTER LUA_CORE_SRC EXCLUDE REGEX "ltests\\.c$")
file(GLOB LUA_LIB_SRC "${lua502_SOURCE_DIR}/src/lib/*.c")
add_library(lua50 STATIC ${LUA_CORE_SRC} ${LUA_LIB_SRC})
target_include_directories(lua50 PUBLIC "${lua502_SOURCE_DIR}/include")
target_include_directories(lua50 PRIVATE "${lua502_SOURCE_DIR}/src")
set_target_properties(lua50 PROPERTIES LINKER_LANGUAGE C)
target_compile_definitions(lua50 PRIVATE LUA_OPNAMES)  # luaP_opnames, for luac's listing
if(NOT MSVC AND NOT EMSCRIPTEN AND CHOCO_SANITIZE)
  # Lua 5.0.2 casts null and not-yet-initialised pointers through its GCObject union (address arithmetic only) and converts
  # any number key to int to test whether it is an array index; keep UBSan from reporting those.
  target_compile_options(lua50 PRIVATE "$<$<CONFIG:Debug>:-fno-sanitize=null,alignment,float-cast-overflow>")
endif()

# The patched luac, used to check animationscript.lua against the compiled chunk in the Mac build.
# Note: the patched loader expects the game's opcode dialect, so chunks written by this luac (or string.dump) use stock
# numbering and no longer load. Nothing in the engine or the game data uses dumped chunks.
if(NOT EMSCRIPTEN)  # a host tool; the web build (cmake/Web.cmake) has no use for it
  add_executable(luac "${lua502_SOURCE_DIR}/src/luac/luac.c" "${lua502_SOURCE_DIR}/src/luac/print.c")
  target_include_directories(luac PRIVATE "${lua502_SOURCE_DIR}/src")
  target_link_libraries(luac PRIVATE lua50)
  if(MSVC)
    target_compile_options(luac PRIVATE /w)
  else()
    target_compile_options(luac PRIVATE -w)
  endif()
endif()
if(NOT MSVC)
  target_compile_options(lua50 PRIVATE -w)
  target_link_libraries(lua50 PUBLIC m)
else()
  target_compile_definitions(lua50 PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(lua50 PRIVATE /w)
endif()

# ---- Engine ----
function(choco_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(CHOCO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # GCC 12+ at -O3 reports -Wrestrict on "literal" + std::string temporaries inside libstdc++ (GCC bug 105329).
      target_compile_options(${target} PRIVATE -Wno-restrict)
    endif()
    if(CHOCO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# prelude.lua and host.lua become C arrays in the binary, so --assets holds only game data.
set(EMBEDDED_LUA_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_lua.cpp")
add_custom_command(
  OUTPUT "${EMBEDDED_LUA_CPP}"
  COMMAND ${CMAKE_COMMAND} -DOUT=${EMBEDDED_LUA_CPP} -DPRELUDE=${CMAKE_CURRENT_SOURCE_DIR}/lua/prelude.lua
          -DHOST=${CMAKE_CURRENT_SOURCE_DIR}/lua/host.lua
          -DANIMSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/lua/animationscript.lua -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedLua.cmake
  DEPENDS lua/prelude.lua lua/host.lua lua/animationscript.lua cmake/EmbedLua.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Embedding prelude.lua and host.lua")

# The window icon (packaging/icon/window128.png) becomes a C array, like the Lua files.
set(EMBEDDED_ICON_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_icon.cpp")
add_custom_command(
  OUTPUT "${EMBEDDED_ICON_CPP}"
  COMMAND ${CMAKE_COMMAND} -DOUT=${EMBEDDED_ICON_CPP} -DPNG=${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/window128.png
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedIcon.cmake
  DEPENDS packaging/icon/window128.png cmake/EmbedIcon.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Embedding the window icon")

# stb_vorbis (public domain, v1.22) as a C static library with warnings off, like Lua. Only the memory decoder is used.
add_library(stb_vorbis STATIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_vorbis.c")
target_compile_definitions(stb_vorbis PRIVATE STB_VORBIS_NO_STDIO STB_VORBIS_NO_PUSHDATA_API)
set_target_properties(stb_vorbis PROPERTIES LINKER_LANGUAGE C)
if(MSVC)
  target_compile_options(stb_vorbis PRIVATE /w)
else()
  target_compile_options(stb_vorbis PRIVATE -w)
  target_link_libraries(stb_vorbis PUBLIC m)
endif()

# Engine sources (docs/design/engine-and-games.md section 7): everything outside games/ except the registry.
set(CHOCO_ENGINE_SOURCES
  ${EMBEDDED_LUA_CPP}
  ${EMBEDDED_ICON_CPP}
  src/script/Bind.cpp
  src/script/ContentNatives.cpp
  src/script/CoreNatives.cpp
  src/script/Formats.cpp
  src/script/Loaders.cpp
  src/script/LuaState.cpp
  src/script/LuaStyle.cpp
  src/script/Messages.cpp
  src/script/RunFile.cpp
  src/script/ScriptContext.cpp
  src/script/ScriptHost.cpp
  src/script/Stubs.cpp
  src/games/Registry.cpp
  src/script/TextNatives.cpp
  src/script/UserNatives.cpp
  src/script/AudioNatives.cpp
  src/script/WindowNatives.cpp
  src/sdk/HiscoreStore.cpp
  src/sdk/TextSprite.cpp
  src/sdk/Users.cpp
  src/sdk/Sprite.cpp
  src/sdk/SpriteScript.cpp
  src/audio/Audio.cpp
  src/audio/CadiLite.cpp
  src/audio/EventTable.cpp
  src/audio/Mixer.cpp
  src/audio/OggStream.cpp
  src/audio/Pcm.cpp
  src/audio/SoundCache.cpp
  src/app/App.cpp
  src/app/Args.cpp
  src/app/Finder.cpp
  src/app/ScriptDriver.cpp
  src/assets/AssetFS.cpp
  src/assets/AssetSource.cpp
  src/assets/Content.cpp
  src/assets/Xml.cpp
  src/gfx/AnmSheet.cpp
  src/gfx/Curve.cpp
  src/gfx/HitMask.cpp
  src/gfx/Image.cpp
  src/gfx/Quad.cpp
  src/gfx/RecordingRenderer.cpp
  src/gfx/SheetCache.cpp
  src/gfx/SpriteSheet.cpp
  src/gfx/TextureCache.cpp
  src/gfx/text/Font.cpp
  src/gfx/text/GlyphAtlas.cpp
  src/gfx/text/Raster.cpp
  src/gfx/text/TextLayout.cpp
  src/platform/AudioDevice.cpp
  src/platform/Console.cpp
  src/platform/SdlRenderer.cpp
  src/platform/DisplayMode.cpp
  src/platform/Platform.cpp
  src/platform/Png.cpp
  src/platform/UserDir.cpp
  src/platform/Window.cpp
  src/ui/Draw.cpp
  src/ui/Input.cpp
  src/ui/Layout.cpp
  src/ui/Style.cpp
  src/ui/Transition.cpp
  src/ui/widgets/CreditsWindow.cpp
  src/ui/widgets/InputWidgets.cpp
  src/ui/widgets/TextWidgets.cpp
  src/ui/widgets/Widgets.cpp
  src/ui/Window.cpp
  src/ui/WindowManager.cpp
  src/app/Diagnostics.cpp
)

# Shared gameplay, always built.
set(CHOCO_COMMON_SOURCES
  src/games/common/Ledger.cpp
  src/games/common/Calendar.cpp
  src/games/common/DynamicWindow.cpp
  src/games/common/Crates.cpp
  src/games/common/Appearance.cpp
  src/games/common/Textures.cpp
  src/games/common/factory/Chocolate.cpp
  src/games/common/factory/FactoryWindow.cpp
  src/games/common/factory/Parts.cpp
  src/games/common/MapWindow.cpp
  src/games/common/Travel.cpp
  src/games/common/sprite/PathSprite.cpp
  src/games/common/sprite/PulseSprite.cpp
  src/games/common/PortDef.cpp
  src/games/common/PortWindow.cpp
  src/games/common/World.cpp
  src/games/common/LegacyUtilNatives.cpp
  src/games/common/OptionsPanel.cpp
)

# Game modules. choco_game_module(<id> [REQUIRES <id>...] SOURCES <files relative to src/games/<id>/> TESTS <files relative to
# tests/>) appends the module's sources and unit tests unless CHOCO_GAME_<ID> is OFF (choc3 has no option), and defines
# CHOCO_GAME_<ID> for the registry and the tests.
option(CHOCO_GAME_CHOC2 "Build Chocolatier 2 support" ON)
option(CHOCO_GAME_CHOC1 "Build Chocolatier 1 support" ON)
# The Great Chocolate Chase (docs/design/greatcc.md decision D2): on since level 1 plays from the first customer to the results.
option(CHOCO_GAME_GREATCC "Build The Great Chocolate Chase support" ON)
set(CHOCO_GAME_SOURCES)
set(CHOCO_GAME_TESTS)
set(CHOCO_GAME_DEFS)
function(choco_game_module id)
  cmake_parse_arguments(M "" "" "REQUIRES;SOURCES;TESTS" ${ARGN})
  string(TOUPPER "${id}" up)
  if(NOT id STREQUAL "choc3" AND NOT CHOCO_GAME_${up})
    return()
  endif()
  foreach(req IN LISTS M_REQUIRES)
    string(TOUPPER "${req}" rup)
    if(NOT req STREQUAL "choc3" AND NOT CHOCO_GAME_${rup})
      message(FATAL_ERROR "game module ${id} requires ${req}, which is switched off")
    endif()
  endforeach()
  set(src ${CHOCO_GAME_SOURCES})
  foreach(f IN LISTS M_SOURCES)
    list(APPEND src "src/games/${id}/${f}")
  endforeach()
  set(tst ${CHOCO_GAME_TESTS})
  foreach(f IN LISTS M_TESTS)
    list(APPEND tst "tests/${f}")
  endforeach()
  set(CHOCO_GAME_SOURCES ${src} PARENT_SCOPE)
  set(CHOCO_GAME_TESTS ${tst} PARENT_SCOPE)
  set(CHOCO_GAME_DEFS ${CHOCO_GAME_DEFS} CHOCO_GAME_${up}=1 PARENT_SCOPE)
endfunction()
include(src/games/choc3/module.cmake)
include(src/games/choc2/module.cmake)
include(src/games/choc1/module.cmake)
include(src/games/greatcc/module.cmake)

add_library(choco_core STATIC ${CHOCO_ENGINE_SOURCES} ${CHOCO_COMMON_SOURCES} ${CHOCO_GAME_SOURCES})
target_compile_definitions(choco_core PUBLIC ${CHOCO_GAME_DEFS})
target_include_directories(choco_core PUBLIC src)
target_include_directories(choco_core SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(choco_core PUBLIC SDL3::SDL3-static pugixml-static lua50 stb_vorbis)
target_compile_definitions(choco_core PRIVATE CHOCO_VERSION="${PROJECT_VERSION}")  # --version, app/Args.cpp
choco_warnings(choco_core)

add_executable(openconche src/app/main.cpp)
# No console window on Windows; --headless/--script re-attach to the parent console (platform/Console.cpp).
if(WIN32)
  set_target_properties(openconche PROPERTIES WIN32_EXECUTABLE TRUE)
  # The executable's icon (Explorer, taskbar): packaging/icon/openconche.ico through a resource script.
  configure_file(packaging/icon/openconche.rc.in "${CMAKE_BINARY_DIR}/packaging/openconche.rc" @ONLY)
  target_sources(openconche PRIVATE "${CMAKE_BINARY_DIR}/packaging/openconche.rc")
endif()
target_link_libraries(openconche PRIVATE choco_core)
choco_warnings(openconche)
if(EMSCRIPTEN)
  # The browser build: the engine as WebAssembly plus a page and the game data as packages. No tests, no tools.
  include(cmake/Web.cmake)
  return()
endif()

enable_testing()
add_executable(lua_compile_all tests/lua_compile_all.cpp)
target_link_libraries(lua_compile_all PRIVATE choco_core)
choco_warnings(lua_compile_all)
# The Reforged checkout lives next to the engine, not in it; without it the test is not generated.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets")
  add_test(NAME lua_compiles_all
    COMMAND lua_compile_all ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets)
endif()
# Where the original game installs are, for the tests that read them (always read only; a test is skipped when its folder
# is not set or not there). A `local_assets.cmake` next to this file may preset them with set(... CACHE PATH ...).
include(${CMAKE_CURRENT_SOURCE_DIR}/local_assets.cmake OPTIONAL)
set(CHOCO_VANILLA_ROOT "" CACHE PATH "Chocolatier 3 Windows install root (its assets/ folder)")
set(CHOCO_MAC_ASSET_DIR "" CACHE PATH "Chocolatier 3 Macintosh assets folder (has cadi/cadilite.spec)")
set(CHOCO_CHOC2_ASSETS "" CACHE PATH "Chocolatier 2 asset folder (assets.pfp, settings.xml, ...) for the choc2 tests")
set(CHOCO_CHOC1_ASSETS "" CACHE PATH "Chocolatier 1 asset folder (assets.pfp, settings.xml, ...) for the choc1 tests")
# The Great Chocolate Chase: the Windows install's assets/ (assets.pfp + loose settings.xml) and the Mac bundle's loose assets/.
set(CHOCO_GREATCC_ASSETS "" CACHE PATH "Great Chocolate Chase Windows asset folder (assets.pfp, settings.xml, ...) for the greatcc tests")
set(CHOCO_GREATCC_MAC_ASSETS "" CACHE PATH "Great Chocolate Chase Macintosh asset folder (loose files, settings.xml) for the greatcc tests")
# The vanilla archive (re/ is not in every checkout); lua_compile_all also accepts an assets directory with assets.pfp.
if(CHOCO_VANILLA_ROOT AND EXISTS "${CHOCO_VANILLA_ROOT}/assets/assets.pfp")
  add_test(NAME lua_compiles_all_pfp
    COMMAND lua_compile_all ${CHOCO_VANILLA_ROOT}/assets)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME vanilla_mainmenu
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_vanilla_mainmenu.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_VANILLA_ROOT}/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/vanilla_mainmenu)
    # The game switcher's entries (docs/design/game-finder.md section 2a): the chord, its Cmd variant, the badge.
    add_test(NAME switcher
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_switcher.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_VANILLA_ROOT}/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/switcher)
    add_test(NAME vanilla_hiscore
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hiscore.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_VANILLA_ROOT}/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/vanilla_hiscore
              --dataset vanilla)
    foreach(scenario factory endtoend)
      add_test(NAME vanilla_${scenario}
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_vanilla_endtoend.py
                --openconche $<TARGET_FILE:openconche>
                --assets ${CHOCO_VANILLA_ROOT}/assets
                --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/vanilla_${scenario}
                --scenario ${scenario})
    endforeach()
    add_test(NAME vanilla_audio
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_audio.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_VANILLA_ROOT}/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/vanilla_audio --dataset vanilla)
    add_test(NAME vanilla_credits
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_credits.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_VANILLA_ROOT}/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/vanilla_credits --case credits --twocol)
  endif()
endif()
add_executable(engine_tests tests/test_main.cpp tests/test_args.cpp tests/test_assets.cpp tests/test_display.cpp tests/test_script.cpp tests/test_bind.cpp tests/test_host.cpp tests/test_gfx.cpp tests/test_ui.cpp tests/test_buttons.cpp tests/test_layout_draw.cpp tests/test_text.cpp tests/test_textwidgets.cpp tests/test_transitions.cpp tests/test_inputwidgets.cpp tests/test_users.cpp tests/test_curve.cpp tests/test_sheets.cpp tests/test_sprite.cpp tests/test_port.cpp tests/test_ledger.cpp tests/test_map.cpp tests/test_crates.cpp tests/test_factory.cpp tests/test_stubs.cpp tests/test_recipe_designer.cpp tests/test_pfp.cpp tests/test_hiscore.cpp tests/test_credits.cpp tests/test_particles.cpp tests/test_utf8_paths.cpp tests/test_cad.cpp tests/test_audio.cpp tests/test_audio_wiring.cpp tests/test_cadilite.cpp tests/test_prefs_import.cpp tests/test_boot_trace.cpp tests/test_calendar.cpp tests/test_finder.cpp
  ${CHOCO_GAME_TESTS})
target_link_libraries(engine_tests PRIVATE choco_core doctest::doctest)
target_include_directories(engine_tests SYSTEM PRIVATE "${lua502_SOURCE_DIR}/src")
target_compile_definitions(engine_tests PRIVATE
  CHOCO_REFORGED_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets"
  CHOCO_VANILLA_ASSETS="${CHOCO_VANILLA_ROOT}/assets"
  CHOCO_MAC_ASSETS="${CHOCO_MAC_ASSET_DIR}"
  CHOCO_GREATCC_ASSETS="${CHOCO_GREATCC_ASSETS}"
  CHOCO_GREATCC_MAC_ASSETS="${CHOCO_GREATCC_MAC_ASSETS}"
  CHOCO_RE_DOCS="${CMAKE_CURRENT_SOURCE_DIR}/docs/re"
  CHOCO_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
  CHOCO_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
  CHOCO_TEST_OUT="${CMAKE_CURRENT_BINARY_DIR}/test_out"
  CHOCO_GFX_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/gfx")
if(MSVC)
  target_compile_definitions(engine_tests PRIVATE _CRT_SECURE_NO_WARNINGS)  # std::getenv in test_audio.cpp
endif()
choco_warnings(engine_tests)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC 12's -O2 inlining of std::variant<..., std::string> moves reports "maybe uninitialized" on correct code
  # (MapStyle::append in test_sprite.cpp). Tests only: engine code keeps the warning.
  target_compile_options(engine_tests PRIVATE -Wno-maybe-uninitialized)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets/cadi")
  add_test(NAME unit COMMAND engine_tests)
else()
  add_test(NAME unit COMMAND ${CMAKE_COMMAND} -E env CHOCO_ASSETS_OPTIONAL=1 $<TARGET_FILE:engine_tests>)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  foreach(s 1 2)
    add_test(NAME scene_scale${s}
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scene.py
              --openconche $<TARGET_FILE:openconche> --mode windowed --scale ${s}
              --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/scene.script
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/scale${s})
  endforeach()
  add_test(NAME scene_resize
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scene.py
            --openconche $<TARGET_FILE:openconche> --mode resize
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/resize)
  foreach(d 2560x1440 1000x1000 1920x1080)
    add_test(NAME scene_fullscreen_${d}
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scene.py
              --openconche $<TARGET_FILE:openconche> --mode fullscreen --display-size ${d}
              --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/fullscreen.script
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/fullscreen_${d})
  endforeach()
  add_test(NAME animationscript_bytecode
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_animationscript.py
            --luac $<TARGET_FILE:luac> --source ${CMAKE_CURRENT_SOURCE_DIR}/lua/animationscript.lua
            --listing ${CMAKE_CURRENT_SOURCE_DIR}/re/mac/lua/animationscript.lst
            --disassembler ${CMAKE_CURRENT_SOURCE_DIR}/workbench/re/lua50_chunks.py
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/animationscript)
  set_tests_properties(animationscript_bytecode PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME layering
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layering.py --root ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME stubs_up_to_date
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_stubs.py --check)
  # Chocolatier 2: present only where the original install is
  # (read only), so a checkout without it has no such test.
  if(CHOCO_GAME_CHOC2 AND EXISTS "${CHOCO_CHOC2_ASSETS}/assets.pfp")
    add_test(NAME choc2_mainmenu
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_mainmenu.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_mainmenu)
    add_test(NAME choc2_options
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_options.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_options)
    add_test(NAME choc2_story
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_story.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_story)
    add_test(NAME choc2_ledger
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_ledger.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_ledger)
    add_test(NAME choc2_travel
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_travel.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_travel)
    add_test(NAME choc2_audio
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_audio.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_audio)
    add_test(NAME choc2_logo
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_logo.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_logo)
    add_test(NAME choc2_factory
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_factory.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_factory)
    set_tests_properties(choc2_factory PROPERTIES TIMEOUT 600)
    add_test(NAME choc2_trade
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_trade.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_trade)
    add_test(NAME choc2_enter
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_enter.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_enter)
    add_test(NAME choc2_dice
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_dice.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_dice)
    add_test(NAME choc2_bonus
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc2_bonus.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC2_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc2_bonus)
  endif()
  # Chocolatier 1 (docs/re/choc1-fit.md): the game's own asset folder, read only.
  if(CHOCO_GAME_CHOC1 AND EXISTS "${CHOCO_CHOC1_ASSETS}/assets.pfp")
    add_test(NAME choc1_mainmenu
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc1_mainmenu.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC1_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc1_mainmenu)
    add_test(NAME choc1_story
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc1_story.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC1_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc1_story)
    add_test(NAME choc1_dice
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc1_dice.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC1_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc1_dice)
    add_test(NAME choc1_finale
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_choc1_finale.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_CHOC1_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/choc1_finale)
    set_tests_properties(choc1_dice PROPERTIES TIMEOUT 600)
  endif()
  # The Great Chocolate Chase (docs/design/greatcc.md section 6): the Windows install (assets.pfp) and the Mac bundle's loose
  # tree each boot to the main menu; present only where the original is (read only).
  if(CHOCO_GAME_GREATCC AND EXISTS "${CHOCO_GREATCC_ASSETS}/assets.pfp")
    add_test(NAME greatcc_boot
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_boot.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_boot)
    add_test(NAME greatcc_dialogs
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_dialogs.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_dialogs)
    add_test(NAME greatcc_menus
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_menus.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_menus)
    add_test(NAME greatcc_users
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_users.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_users)
    add_test(NAME greatcc_scene
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_scene.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_scene)
    add_test(NAME greatcc_audio
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_audio.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_audio)
    add_test(NAME greatcc_worldmap
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_worldmap.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_worldmap)
    add_test(NAME greatcc_shop
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_shop.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_shop)
    add_test(NAME greatcc_walk
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_walk.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_walk)
    add_test(NAME greatcc_machines
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_machines.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_machines)
    add_test(NAME greatcc_stations
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_stations.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_stations)
    add_test(NAME greatcc_level1
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_level1.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_level1)
    set_tests_properties(greatcc_level1 PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_tutorial1
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_tutorial1.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_tutorial1)
    set_tests_properties(greatcc_tutorial1 PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_prelevel
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_prelevel.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_prelevel)
    set_tests_properties(greatcc_prelevel PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_meta
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_meta.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_meta)
    set_tests_properties(greatcc_meta PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_endless
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_endless.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_endless)
    set_tests_properties(greatcc_endless PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_scores
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_scores.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_scores)
    set_tests_properties(greatcc_scores PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_debug
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_debug.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_debug)
    set_tests_properties(greatcc_debug PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_story_smoke
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_story_smoke.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_story_smoke)
    set_tests_properties(greatcc_story_smoke PROPERTIES TIMEOUT 1800)
  endif()
  if(CHOCO_GAME_GREATCC AND EXISTS "${CHOCO_GREATCC_MAC_ASSETS}/settings.xml")
    add_test(NAME greatcc_boot_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_boot.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_boot_mac)
    add_test(NAME greatcc_dialogs_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_dialogs.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_dialogs_mac)
    add_test(NAME greatcc_menus_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_menus.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_menus_mac)
    add_test(NAME greatcc_users_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_users.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_users_mac)
    add_test(NAME greatcc_scene_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_scene.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_scene_mac)
    add_test(NAME greatcc_audio_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_audio.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_audio_mac)
    add_test(NAME greatcc_worldmap_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_worldmap.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_worldmap_mac)
    add_test(NAME greatcc_shop_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_shop.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_shop_mac)
    add_test(NAME greatcc_walk_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_walk.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_walk_mac)
    add_test(NAME greatcc_machines_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_machines.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_machines_mac)
    add_test(NAME greatcc_stations_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_stations.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_stations_mac)
    add_test(NAME greatcc_level1_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_level1.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_level1_mac)
    set_tests_properties(greatcc_level1_mac PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_tutorial1_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_tutorial1.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_tutorial1_mac)
    set_tests_properties(greatcc_tutorial1_mac PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_prelevel_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_prelevel.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_prelevel_mac)
    set_tests_properties(greatcc_prelevel_mac PROPERTIES TIMEOUT 900)
    add_test(NAME greatcc_meta_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_meta.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_meta_mac)
    set_tests_properties(greatcc_meta_mac PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_endless_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_endless.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_endless_mac)
    set_tests_properties(greatcc_endless_mac PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_scores_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_scores.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_scores_mac)
    set_tests_properties(greatcc_scores_mac PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_debug_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_debug.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_debug_mac)
    set_tests_properties(greatcc_debug_mac PROPERTIES TIMEOUT 1800)
    add_test(NAME greatcc_story_smoke_mac
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_greatcc_story_smoke.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CHOCO_GREATCC_MAC_ASSETS}
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/greatcc_story_smoke_mac)
    set_tests_properties(greatcc_story_smoke_mac PROPERTIES TIMEOUT 1800)
  endif()
  add_test(NAME audio_events_up_to_date
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_audio_events.py --check)
  # The game finder's folder-resolution probe (docs/design/game-finder.md section 4): present only where the
  # originals are (read only). Runs the --pick-game/--pick-dir path, not the GUI screen.
  set(CHOCO_FINDER_REFORGED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged" CACHE PATH
      "The Reforged checkout root (its assets/ folder) for the finder test")
  set(CHOCO_FINDER_VANILLA_ROOT "${CHOCO_VANILLA_ROOT}" CACHE PATH
      "The Chocolatier 3 Windows install root for the finder test")
  if(CHOCO_FINDER_VANILLA_ROOT AND CHOCO_CHOC2_ASSETS AND CHOCO_CHOC1_ASSETS AND EXISTS "${CHOCO_FINDER_VANILLA_ROOT}/assets/settings.xml")
    get_filename_component(CHOCO_FINDER_CHOC2_ROOT "${CHOCO_CHOC2_ASSETS}" DIRECTORY)
    get_filename_component(CHOCO_FINDER_CHOC1_ROOT "${CHOCO_CHOC1_ASSETS}" DIRECTORY)
    add_test(NAME finder
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_finder.py
              --openconche $<TARGET_FILE:openconche>
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/finder
              REFORGED=${CHOCO_FINDER_REFORGED_ROOT}
              VANILLA3=${CHOCO_FINDER_VANILLA_ROOT}
              CHOC2=${CHOCO_FINDER_CHOC2_ROOT}
              CHOC1=${CHOCO_FINDER_CHOC1_ROOT})
  endif()

  # The Reforged scenario tests all mount the checkout's assets, which live next to the engine, not in it
  # (a fresh clone has none; its suite stays green without them). The audio_names scan reads the same
  # checkout, so it rides the same guard.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets")
    add_test(NAME reforged_boot
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/boot)
    add_test(NAME reforged_mainmenu
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_mainmenu.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --test-assets ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/mainmenu)
    add_test(NAME reforged_utf8_paths
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_utf8_paths.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/utf8_paths)
    add_test(NAME reforged_menu_screenshot
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_menu_screenshot.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --reference ${CMAKE_CURRENT_SOURCE_DIR}/reference
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/menu_screenshot)
    add_test(NAME reforged_story_button
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_story_button.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/story_button)
    add_test(NAME reforged_new_player
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_input_screens.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/new_player
              --case new_player --displays scale1,scale2,fullscreen)
    add_test(NAME reforged_options
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_input_screens.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/options
              --case options --displays scale1,scale2,fullscreen)
    add_test(NAME reforged_help
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_input_screens.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/help
              --case help --displays scale1)
    add_test(NAME reforged_change_player
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_input_screens.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/change_player
              --case change_player --displays scale1)
    add_test(NAME reforged_transitions
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_transitions.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --reference ${CMAKE_CURRENT_SOURCE_DIR}/reference
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/transitions)
    add_test(NAME reforged_new_user
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_new_user.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/new_user)
    add_test(NAME prefs_import
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_prefs_import.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --vector ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/prefs/ada_bob.dat
              --save ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/prefs/ada_save.txt
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/prefs_import)
    add_test(NAME reforged_port_zurich
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_port.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/port)
    add_test(NAME reforged_diag
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_diag.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/diag)
    add_test(NAME reforged_ledger
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_ledger.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/ledger)
    add_test(NAME reforged_endtoend
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_endtoend.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/endtoend
              --displays scale1,fullscreen)
    add_test(NAME reforged_crates
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_crates.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/crates)
    add_test(NAME reforged_focus
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_focus.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/focus)
    add_test(NAME reforged_quit_event
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_quit_event.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/quit_event)
    add_test(NAME reforged_exit_save_guard
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_save_guard.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/exit_save_guard)
    add_test(NAME reforged_audio
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_audio.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/audio)
    set(CHOCO_AUDIO_NAMES_TREES --lua ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets)
    if(CHOCO_MAC_ASSET_DIR AND EXISTS ${CHOCO_MAC_ASSET_DIR}/cadi/cadilite.spec)
      list(APPEND CHOCO_AUDIO_NAMES_TREES --lua ${CHOCO_MAC_ASSET_DIR})
    endif()
    add_test(NAME audio_names
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_audio_names.py ${CHOCO_AUDIO_NAMES_TREES})
    # The Macintosh build's audio comes from its own cadi/cadilite.spec (docs/design/mac-audio.md); the check runs only
    # where those assets are present.
    if(CHOCO_MAC_ASSET_DIR AND EXISTS ${CHOCO_MAC_ASSET_DIR}/cadi/cadilite.spec)
      add_test(NAME mac_audio
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_audio.py
                --openconche $<TARGET_FILE:openconche>
                --assets ${CHOCO_MAC_ASSET_DIR}
                --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/mac_audio --dataset mac)
    endif()
    add_test(NAME reforged_slotmachine
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_slotmachine.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/slotmachine)
    add_test(NAME reforged_hiscore
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hiscore.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/hiscore_reforged
              --dataset reforged)
    add_test(NAME reforged_particles
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_particles.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/particles)
    add_test(NAME reforged_factory_chocolate
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_factory.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/factory_chocolate
              --displays scale1,fullscreen --twice)
    set_tests_properties(reforged_factory_chocolate PROPERTIES TIMEOUT 900)
    add_test(NAME reforged_factory_coffee
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_factory.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/factory_coffee
              --game coffee --displays scale1,fullscreen --twice)
    set_tests_properties(reforged_factory_coffee PROPERTIES TIMEOUT 900)
    foreach(c credits history)
      add_test(NAME reforged_${c}
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_credits.py
                --openconche $<TARGET_FILE:openconche>
                --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
                --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/${c} --case ${c})
    endforeach()
    add_test(NAME reforged_recipe_designer
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_recipe_designer.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/recipe_designer
              --displays scale1,fullscreen)
    foreach(c map travel kitchen)
      add_test(NAME reforged_${c}
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_map.py
                --openconche $<TARGET_FILE:openconche>
                --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
                --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/${c}
                --case ${c} --displays scale1,fullscreen)
      # The fullscreen run is ~1900 software-rendered 2560x1440 frames: about 150 s alone, several times that under load.
      set_tests_properties(reforged_${c} PROPERTIES TIMEOUT 1800)
    endforeach()
    add_test(NAME reforged_devmenu
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_devmenu.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/devmenu)
    set_tests_properties(reforged_devmenu PROPERTIES TIMEOUT 900)
    add_test(NAME reforged_run
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_run.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --playtest ${CMAKE_CURRENT_SOURCE_DIR}/tools/playtest
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/run --case casino)
    add_test(NAME reforged_playtest_snippets
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_run.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --playtest ${CMAKE_CURRENT_SOURCE_DIR}/tools/playtest
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/playtest
              --case factory --case designer --case rich --case freeplay)
    set_tests_properties(reforged_playtest_snippets PROPERTIES TIMEOUT 900)
    add_test(NAME reforged_menu_text
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_menu_text.py
              --openconche $<TARGET_FILE:openconche>
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/menu_text)
  endif()

  foreach(s 1 2)
    add_test(NAME gfx_scene_scale${s}
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scene.py
              --openconche $<TARGET_FILE:openconche> --mode windowed --scale ${s} --gfx
              --assets ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/gfx
              --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/gfx.script
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/gfx${s})
  endforeach()
  add_test(NAME gfx_scene_fullscreen
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scene.py
            --openconche $<TARGET_FILE:openconche> --mode fullscreen --display-size 2560x1440 --gfx
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/gfx
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/gfx_fullscreen)
  # Fonts: the scene needs two shipped fonts from the Reforged assets, copied at test time (skipped when absent).
  foreach(m windowed1 windowed2 fullscreen)
    set(extra --mode windowed --scale 1)
    if(m STREQUAL windowed2)
      set(extra --mode windowed --scale 2)
    elseif(m STREQUAL fullscreen)
      set(extra --mode fullscreen --display-size 2560x1440)
    endif()
    add_test(NAME text_scene_${m}
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_text.py
              --openconche $<TARGET_FILE:openconche> ${extra}
              --reforged ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
              --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/text_${m})
    set_tests_properties(text_scene_${m} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  add_test(NAME text_scene_resize
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_text.py
            --openconche $<TARGET_FILE:openconche> --mode resize
            --reforged ${CMAKE_CURRENT_SOURCE_DIR}/Chocolatier-Decadence-Reforged/assets
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/text_resize)
  set_tests_properties(text_scene_resize PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME cli_behaviour
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.py
            --openconche $<TARGET_FILE:openconche>
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/test_out/cli)
else()
  message(WARNING "Python 3 not found: scripted scenario tests disabled")
endif()

include(cmake/Distribution.cmake)
