cmake_minimum_required(VERSION 3.21)
project(240mp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Strip qDebug(...) calls in Release builds to reduce CPU/disk noise on Pi
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT")

# Release version string. CI passes the git tag (-DAPP_VERSION=vX.Y.Z); local
# builds report "dev", which disables the in-app self-update download path.
set(APP_VERSION "dev" CACHE STRING "Release version (CI passes the git tag)")

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Svg)
find_package(OpenSSL REQUIRED)
find_package(SDL2 CONFIG REQUIRED) # gamepad input (InputManager)

# PC/SC for NFC reader support (optional — module compiles without it but
# reader functionality is disabled at runtime).
if(APPLE)
    find_library(PCSC_LIBRARIES PCSC)
else()
    find_library(PCSC_LIBRARIES pcsclite)
    find_path(PCSC_INCLUDE_DIRS PCSC/winscard.h
        PATHS /usr/include /usr/local/include
    )
endif()
if(PCSC_LIBRARIES)
    set(MPCSC_FOUND TRUE)
else()
    set(MPCSC_FOUND FALSE)
    message(STATUS "PC/SC not found — NFC reader module will compile but reader support is disabled")
endif()

# libdrm is used on Linux/RPi to save and restore the KMS CRTC state
# around mpv playback so Qt EGLFS can resume cleanly after mpv exits.
if(NOT APPLE)
    find_package(PkgConfig REQUIRED)
    if(PkgConfig_FOUND)
        pkg_check_modules(Libdrm REQUIRED libdrm)
    else()
        find_path(Libdrm_INCLUDE_DIRS libdrm/drm.h
            PATHS /usr/include /usr/local/include
        )
        find_library(Libdrm_LIBRARIES
            NAMES drm
            PATHS /usr/lib /usr/local/lib
        )
        if(NOT Libdrm_INCLUDE_DIRS OR NOT Libdrm_LIBRARIES)
            message(FATAL_ERROR "Could not find libdrm. Install libdrm-dev")
        endif()
    endif()
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

add_executable(240mp
    src/main.cpp
    src/AppCore.cpp
    src/modules/local_files/LocalFilesBackend.cpp
    src/modules/plex/PlexBackend.cpp
    src/modules/jellyfin/JellyfinBackend.cpp
    src/modules/ambient_mode/AmbientModeBackend.cpp
    src/modules/youtube/YouTubeBackend.cpp
    src/modules/nfc_reader/NfcReaderBackend.h
    src/modules/nfc_reader/NfcReaderBackend.cpp
    src/player/MpvController.cpp
    src/input/InputManager.cpp
    src/input/IdleTracker.cpp
    src/update/UpdateManager.cpp
    $<$<BOOL:${APPLE}>:src/macos_utils.mm>
)

target_compile_definitions(240mp PRIVATE APP_VERSION="${APP_VERSION}")

target_link_libraries(240mp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Svg
    OpenSSL::Crypto
    SDL2::SDL2
    $<$<BOOL:${MPCSC_FOUND}>:${PCSC_LIBRARIES}>
    $<$<NOT:$<BOOL:${APPLE}>>:${Libdrm_LIBRARIES}>
)

if(APPLE AND MPCSC_FOUND)
    target_link_libraries(240mp PRIVATE "-framework AppKit" ${PCSC_LIBRARIES})
endif()

if(NOT APPLE AND MPCSC_FOUND)
    target_include_directories(240mp PRIVATE
        ${PCSC_INCLUDE_DIRS}
        ${PCSC_INCLUDE_DIRS}/PCSC
    )
endif()

if(NOT APPLE)
    target_include_directories(240mp PRIVATE
        ${Libdrm_INCLUDE_DIRS}
    )
endif()

target_include_directories(240mp PRIVATE
    src
)

if(MPCSC_FOUND)
    target_compile_definitions(240mp PRIVATE MP240_NFC_READER_AVAILABLE=1)
endif()

# ── macOS bundle metadata ──────────────────────────────────────────────────────
set_target_properties(240mp PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "240-MP"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.240mp.app"
    MACOSX_BUNDLE_ICON_FILE "assets/images/240-mp"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# ── Dev bundle symlink (macOS only, non-CI) ───────────────────────────────────
# Makes double-clicking build/240mp.app work during development by symlinking
# Contents/Resources → repo root so resolveAppRoot() finds the QML files.
# Skipped in CI (ENV{CI} is set by GitHub Actions) because cmake --install
# populates Resources with real files — a symlink in the build tree would be
# packaged into the DMG pointing to a CI path that doesn't exist on end-user Macs.
if(APPLE AND NOT DEFINED ENV{CI})
    add_custom_command(TARGET 240mp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "$<TARGET_BUNDLE_CONTENT_DIR:240mp>/Resources"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_SOURCE_DIR}"
            "$<TARGET_BUNDLE_CONTENT_DIR:240mp>/Resources"
        COMMENT "Symlinking repo root as bundle Resources (dev build)"
    )
endif()

# ── Install rules ──────────────────────────────────────────────────────────────
if(APPLE)
    install(TARGETS 240mp BUNDLE DESTINATION . COMPONENT Runtime)

    # Use a hardcoded relative path — $<TARGET_BUNDLE_CONTENT_DIR> resolves to
    # the build tree, not the install prefix, inside install() rules.
    install(FILES "${CMAKE_SOURCE_DIR}/Main.qml"
        DESTINATION "240mp.app/Contents/Resources"
        COMPONENT Runtime)

    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/views"
        "${CMAKE_SOURCE_DIR}/modules"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/scripts"
        DESTINATION "240mp.app/Contents/Resources"
        COMPONENT Runtime)

    # macdeployqt is NOT run here — it requires Qt frameworks to be present and
    # is environment-specific. Run it manually in CI after cmake --install.
else()
    install(TARGETS 240mp
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime)

    install(FILES "${CMAKE_SOURCE_DIR}/Main.qml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/240mp"
        COMPONENT Runtime)

    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/views"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/scripts"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/240mp"
        COMPONENT Runtime)

    install(DIRECTORY "${CMAKE_SOURCE_DIR}/modules"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/240mp"
        COMPONENT Runtime)
endif()

# ── CPack ──────────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "240-MP")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "240-MP Media Player")
set(CPACK_PACKAGE_VENDOR "240-MP")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "240-MP")
    set(CPACK_DMG_FORMAT "UDZO")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
endif()

include(CPack)
