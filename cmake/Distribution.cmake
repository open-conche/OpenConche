# The shipping build. Active only with -DCHOCO_RELEASE=ON (CMakeLists.txt sets the build type, the macOS architecture
# and the MSVC runtime before project(); this file adds the bundle, the install rules and the zip).
#
#   cmake -S . -B build/release -G Ninja -DCHOCO_RELEASE=ON
#   cmake --build build/release --target dist
#
# leaves one archive in build/release/dist/ (a zip, a tar.gz on Linux):
#   openconche-<version>-macos-arm64.zip      OpenConche.app (ad-hoc signed; the game binary is inside it)
#   openconche-<version>-windows-x64.zip      openconche.exe, static runtime, no DLLs
#   openconche-<version>-linux-x86_64.tar.gz  openconche, static libstdc++/libgcc; SDL loads X11/Wayland at run time
# each with LICENSE.txt, THIRD_PARTY.txt and a README.txt. The engine links SDL3, Lua, pugixml and stb statically,
# so nothing else travels with the executable.
if(NOT CHOCO_RELEASE)
  return()
endif()

set(CHOCO_DIST_DIR "${CMAKE_BINARY_DIR}/dist")

# ---- Per-platform shape of the executable ----
if(APPLE)
  set(CHOCO_ICNS "${CMAKE_SOURCE_DIR}/packaging/icon/OpenConche.icns")
  set_source_files_properties("${CHOCO_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(openconche PRIVATE "${CHOCO_ICNS}")
  set_target_properties(openconche PROPERTIES
    OUTPUT_NAME "OpenConche"
    MACOSX_BUNDLE_ICON_FILE "OpenConche.icns"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "OpenConche"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.openconche.engine"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "MIT License, The OpenConche Authors")
  # Apple silicon refuses an unsigned binary; the linker signs ad hoc, but the bundle as a whole (Info.plist included)
  # needs its own seal or Finder reports it damaged. Not a Developer ID and not notarised: Gatekeeper blocks the
  # downloaded app until its quarantine attribute is removed (README.txt says how).
  add_custom_command(TARGET openconche POST_BUILD
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:openconche>"
    COMMENT "Ad-hoc signing OpenConche.app")
  set(CHOCO_DIST_OS macos)
  set(CHOCO_DIST_ARCH arm64)
  set(CHOCO_DIST_RUN "The app is not signed with an Apple developer certificate, so macOS refuses to open it as\ndownloaded (\"damaged\" or \"cannot be opened\"). Clear the quarantine flag once, in Terminal, from this folder:\n\n    xattr -dr com.apple.quarantine OpenConche.app\n\nThen double-click OpenConche.app.")
elseif(WIN32)
  set(CHOCO_DIST_OS windows)
  set(CHOCO_DIST_ARCH x64)
  set(CHOCO_DIST_RUN "Double-click openconche.exe.")
else()
  # Carry the C++ runtime inside the binary so the zip runs on distributions older or newer than the build machine.
  target_link_options(openconche PRIVATE -static-libstdc++ -static-libgcc)
  set(CHOCO_DIST_OS linux)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CHOCO_DIST_ARCH)
  set(CHOCO_DIST_RUN "./openconche\n\nThe binary needs glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 and later) and uses the X11 or\nWayland, PulseAudio or PipeWire libraries a desktop already has. If your extractor dropped the\nexecute bit: chmod +x openconche")
endif()

# ---- What goes into the archive ----
# Everything is in the `dist` install component, and CPack installs only that component: the fetched libraries
# (pugixml, doctest) bring install rules of their own, which must not land in the zip.
configure_file("${CMAKE_SOURCE_DIR}/packaging/README.txt.in" "${CMAKE_BINARY_DIR}/packaging/README.txt" @ONLY)
install(TARGETS openconche RUNTIME DESTINATION . COMPONENT dist BUNDLE DESTINATION . COMPONENT dist)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt COMPONENT dist)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/THIRD_PARTY.txt" "${CMAKE_BINARY_DIR}/packaging/README.txt" DESTINATION .
        COMPONENT dist)

# ---- The archive ----
set(CHOCO_DIST_NAME "openconche-${PROJECT_VERSION}-${CHOCO_DIST_OS}-${CHOCO_DIST_ARCH}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CHOCO_DIST_EXT "tar.gz")  # the Linux convention, and the execute bit survives every extractor; see the dist target
else()
  set(CHOCO_DIST_EXT "zip")
endif()
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "openconche")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CHOCO_DIST_NAME}")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};dist;/")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")  # staging in build/release/_CPack_Packages
set(CPACK_OUTPUT_FILE_PREFIX "dist")                 # the archive in build/release/dist/
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)  # the zip unpacks into one folder named like the archive
set(CPACK_VERBATIM_VARIABLES ON)
# No CPACK_STRIP_FILES: on macOS stripping after the link would break the ad-hoc signature; Linux strips at install.
include(CPack)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Not CPack's TGZ: a tar records the owner's user and group names, and that would be the build machine's account.
  # GNU tar writes root/root with the same files, from a plain `cmake --install` of the dist component.
  find_program(CHOCO_TAR tar REQUIRED)
  set(stage "${CMAKE_BINARY_DIR}/_dist_stage")
  add_custom_target(dist
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CHOCO_DIST_DIR}" "${stage}/${CHOCO_DIST_NAME}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --component dist --strip
            --prefix "${stage}/${CHOCO_DIST_NAME}" > /dev/null
    COMMAND "${CHOCO_TAR}" --owner=0 --group=0 --numeric-owner --sort=name -C "${stage}"
            -czf "${CHOCO_DIST_DIR}/${CHOCO_DIST_NAME}.tar.gz" "${CHOCO_DIST_NAME}"
    DEPENDS openconche
    COMMENT "Packaging ${CHOCO_DIST_NAME}.tar.gz into ${CHOCO_DIST_DIR}")
else()
  add_custom_target(dist
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CHOCO_DIST_DIR}"
    COMMAND "${CMAKE_CPACK_COMMAND}" -C $<CONFIG> --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS openconche
    COMMENT "Packaging ${CHOCO_DIST_NAME}.${CHOCO_DIST_EXT} into ${CHOCO_DIST_DIR}")
endif()
