# The browser build (Emscripten). Configure with Emscripten's toolchain and build the `web` target:
#
#     emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCHOCO_WEB_GAMES="choc3=/path/to/chocolatier3/assets;choc2=/path/to/chocolatier2/assets"
#     cmake --build build-web --target web
#
# build-web/web/ is then a static site: the engine (openconche.js, openconche.wasm), the page (packaging/web/index.html),
# games.js (the packaged games, with titles for the chooser) and one data package per game (<id>.data with its <id>.js loader, made by
# Emscripten's file_packager from the assets directory, minus CHOCO_WEB_EXCLUDE). The page picks the game by its ?game=<id> query, shows a
# chooser when there is none, and boots the engine with --assets /assets/<id> --userdir /user/<id>. The user folder is an
# IDBFS mount (the browser's IndexedDB), flushed after every save (platform::syncUserDir).
#
# The frame loop is the browser's (emscripten_set_main_loop in app/App.cpp); there are no threads (the audio prefetch
# worker is skipped), so the site needs no cross-origin isolation headers and serves from any static file server.
set(CHOCO_WEB_GAMES "" CACHE STRING "Games to package for the web build: <id>=<assets dir>;<id>=<assets dir>;...")
# Files left out of every package, as file_packager --exclude patterns against the full source path (fnmatch, so `*`
# crosses directories): e.g. "*/languages/*/*;*/fonts/notoserif-sc.mvec" drops the translations and one font.
set(CHOCO_WEB_EXCLUDE "" CACHE STRING "Patterns of files to leave out of the data packages")

target_link_options(openconche PRIVATE
  -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=128MB
  -sSTACK_SIZE=4MB        # the default 64 KB is too small for the script host's nested calls
  -sFORCE_FILESYSTEM=1
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ccall  # the page's user-folder sync and keyboard bridge
  -lidbfs.js
  -sENVIRONMENT=web
  -sEXIT_RUNTIME=0)       # the run ends by leaving for the chooser (App.cpp's webFrame), never by exit()
# SDL's Emscripten backend talks to the page's #canvas element (its default selector).

set(web_dir "${CMAKE_BINARY_DIR}/web")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(packager "${EMSCRIPTEN_ROOT_PATH}/tools/file_packager.py")
if(NOT EXISTS "${packager}")
  message(FATAL_ERROR "Emscripten's file_packager.py not found at ${packager}")
endif()

# The index page's chooser names the games; an id outside this list is shown as itself.
set(title_choc1 "Chocolatier")
set(title_choc2 "Chocolatier 2: Secret Ingredients")
set(title_choc3 "Chocolatier 3: Decadence by Design")
set(title_choc3-reforged "Chocolatier 3: Decadence by Design (Reforged)")
set(title_greatcc "The Great Chocolate Chase")
set(web_outputs)
set(games_js "var CHOCO_GAMES = [")
set(first TRUE)
foreach(entry IN LISTS CHOCO_WEB_GAMES)
  if(NOT entry MATCHES "^([A-Za-z0-9_-]+)=(.+)$")
    message(FATAL_ERROR "CHOCO_WEB_GAMES entry '${entry}' is not <id>=<assets dir>")
  endif()
  set(id "${CMAKE_MATCH_1}")
  set(dir "${CMAKE_MATCH_2}")
  if(NOT IS_DIRECTORY "${dir}")
    message(FATAL_ERROR "CHOCO_WEB_GAMES: '${dir}' (game ${id}) is not a directory")
  endif()
  # The package is rebuilt only when missing (a directory cannot be a dependency): delete <id>.data to repack.
  set(exclude)
  if(CHOCO_WEB_EXCLUDE)
    set(exclude --exclude ${CHOCO_WEB_EXCLUDE})  # one argument per pattern
  endif()
  add_custom_command(
    OUTPUT "${web_dir}/${id}.data" "${web_dir}/${id}.js"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${web_dir}"
    COMMAND ${Python3_EXECUTABLE} -I "${packager}" "${web_dir}/${id}.data"
            --preload "${dir}@/assets/${id}" ${exclude} --js-output=${web_dir}/${id}.js --no-node --use-preload-cache
    COMMENT "Packaging ${id} from ${dir}"
    VERBATIM)
  list(APPEND web_outputs "${web_dir}/${id}.data" "${web_dir}/${id}.js")
  if(NOT first)
    string(APPEND games_js ",")
  endif()
  set(first FALSE)
  set(title "${id}")
  if(DEFINED title_${id})
    set(title "${title_${id}}")
  endif()
  string(APPEND games_js "{id: \"${id}\", title: \"${title}\"}")
endforeach()
string(APPEND games_js "];\n")
file(GENERATE OUTPUT "${web_dir}/games.js" CONTENT "${games_js}")

add_custom_command(
  OUTPUT "${web_dir}/openconche.js" "${web_dir}/openconche.wasm" "${web_dir}/index.html"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${web_dir}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:openconche> $<TARGET_FILE_DIR:openconche>/openconche.wasm
          "${CMAKE_SOURCE_DIR}/packaging/web/index.html" "${web_dir}/"
  DEPENDS openconche "${CMAKE_SOURCE_DIR}/packaging/web/index.html"
  COMMENT "Assembling the site in ${web_dir}"
  VERBATIM)
add_custom_target(web ALL DEPENDS "${web_dir}/openconche.js" ${web_outputs})
