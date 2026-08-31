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

# Optional: the guide's moving preview of what is on a channel. Optional on
# purpose — it is one panel on one screen, and neither the build nor the app
# should fail for want of it. The QML that uses it is loaded through a Loader
# so a build or an install without the module simply shows a still card
# instead. (Debian: qt6-multimedia-dev, qml6-module-qtmultimedia.)
find_package(Qt6 QUIET COMPONENTS Multimedia)
find_package(OpenSSL REQUIRED)
find_package(SDL2 CONFIG REQUIRED) # gamepad input (InputManager)

# libdrm is used on Linux/RPi to save and restore the KMS CRTC state
# around mpv playback so Qt EGLFS can resume cleanly after mpv exits.
if(NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libdrm REQUIRED libdrm)
endif()

# PC/SC for the NFC reader module. Optional: the module's other driver talks to
# a PN532 over USB serial through plain termios, so readers still work without
# it — which is what makes the NFC module usable on immutable distros where
# pcscd cannot be installed. macOS ships PCSC.framework with the OS; Linux needs
# libpcsclite-dev.
if(APPLE)
    find_library(PCSC_LIBRARIES PCSC)
    if(PCSC_LIBRARIES)
        set(PCSC_FOUND TRUE)
    endif()
else()
    pkg_check_modules(PCSC IMPORTED_TARGET libpcsclite)
endif()
if(NOT PCSC_FOUND)
    message(STATUS "PC/SC not found — NFC reader module will build with PN532 USB serial support only")
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
    src/modules/emby/EmbyBackend.cpp
    src/modules/ambient_mode/AmbientModeBackend.cpp
    src/modules/youtube/YouTubeBackend.cpp
    src/modules/nfc_reader/NfcReaderBackend.cpp
    src/modules/nfc_reader/PcscDriver.cpp
    src/modules/nfc_reader/Pn532SerialDriver.cpp
    src/modules/nfc_reader/SerialPort.cpp
    src/modules/weather/WeatherBackend.cpp
    src/modules/scripts/ScriptsBackend.cpp
    src/modules/scripts/ScriptLauncher.cpp
    src/modules/virtual_channels/VirtualChannelsBackend.cpp
    src/modules/virtual_channels/ChannelSchedule.cpp
    src/modules/virtual_channels/ChannelTuner.cpp
    src/modules/virtual_channels/PathGuard.cpp
    src/modules/virtual_channels/ScheduleGenerator.cpp
    src/modules/virtual_channels/DurationProbe.cpp
    src/modules/virtual_channels/MediaServerSource.cpp
    src/modules/virtual_channels/LocalLibrary.cpp
    src/player/MpvController.cpp
    src/util/YtDlpLocator.cpp
    src/util/MpvLocator.cpp
    src/util/ExecPath.cpp
    src/util/DisplayHandoff.cpp
    src/util/FontconfigOverride.cpp
    src/input/InputManager.cpp
    src/input/IdleTracker.cpp
    src/update/UpdateManager.cpp
    $<$<BOOL:${APPLE}>:src/util/MacosUtils.mm>
)

target_compile_definitions(240mp PRIVATE APP_VERSION="${APP_VERSION}")

if(Qt6Multimedia_FOUND)
    target_link_libraries(240mp PRIVATE Qt6::Multimedia)
    message(STATUS "Qt Multimedia found — the guide will show moving previews")
else()
    message(STATUS "Qt Multimedia not found — the guide will show still cards")
endif()

target_link_libraries(240mp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Svg
    OpenSSL::Crypto
    SDL2::SDL2
    $<$<NOT:$<BOOL:${APPLE}>>:${Libdrm_LIBRARIES}>
)

if(APPLE)
    target_link_libraries(240mp PRIVATE "-framework AppKit")
endif()

if(PCSC_FOUND)
    target_compile_definitions(240mp PRIVATE MP240_NFC_PCSC_AVAILABLE=1)
    if(APPLE)
        target_link_libraries(240mp PRIVATE ${PCSC_LIBRARIES})
    else()
        target_link_libraries(240mp PRIVATE PkgConfig::PCSC)
    endif()
endif()

if(NOT APPLE)
    target_include_directories(240mp PRIVATE ${Libdrm_INCLUDE_DIRS})
endif()

target_include_directories(240mp PRIVATE
    src
)

# ── macOS bundle metadata ──────────────────────────────────────────────────────
set_target_properties(240mp PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "240-MP"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.240mp.app"
    MACOSX_BUNDLE_ICON_FILE "packaging/macos/240-mp"
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

    # Icon lives under packaging/macos (not assets/) so it ships alongside the
    # other macOS packaging inputs; installed at the same relative path so it
    # keeps matching MACOSX_BUNDLE_ICON_FILE above.
    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/packaging/macos"
        DESTINATION "240mp.app/Contents/Resources/packaging"
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
        "${CMAKE_SOURCE_DIR}/modules"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/scripts"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/240mp"
        COMPONENT Runtime)
endif()

# ── Tests (opt-in) ─────────────────────────────────────────────────────────────
# OFF by default: the normal build and the release pipeline are unaffected, and
# an upstream PR is not forced to adopt a test framework. QtCore only — these
# cover the scheduling arithmetic, which needs no Quick, no mpv and no display.
#
#   cmake -B build -DBUILD_VCHAN_TESTS=ON
#   cmake --build build --target vchan-tests && ./build/vchan-tests
option(BUILD_VCHAN_TESTS "Build the virtual-channels scheduling tests" OFF)
if(BUILD_VCHAN_TESTS)
    add_executable(vchan-tests
        src/modules/virtual_channels/tests/main.cpp
        src/modules/virtual_channels/tests/ChannelScheduleTest.cpp
        src/modules/virtual_channels/tests/PathGuardTest.cpp
        src/modules/virtual_channels/tests/ChannelTunerTest.cpp
        src/modules/virtual_channels/tests/ScheduleGeneratorTest.cpp
        src/modules/virtual_channels/tests/DurationProbeTest.cpp
        src/modules/virtual_channels/tests/MediaServerSourceTest.cpp
        src/modules/virtual_channels/tests/LocalLibraryTest.cpp
        # Listed so AUTOMOC processes it: it declares a QObject but has no
        # same-named source file for AUTOMOC to pair it with.
        src/modules/virtual_channels/tests/FakeMediaServer.h
        src/modules/virtual_channels/ChannelSchedule.cpp
        src/modules/virtual_channels/PathGuard.cpp
        src/modules/virtual_channels/ChannelTuner.cpp
        src/modules/virtual_channels/ScheduleGenerator.cpp
        src/modules/virtual_channels/DurationProbe.cpp
        src/modules/virtual_channels/MediaServerSource.cpp
        src/modules/virtual_channels/LocalLibrary.cpp)
    # MediaServerSource and the fake backend it is tested against are both
    # QObjects, so this target needs moc even though the rest of the suite is
    # plain C++.
    set_target_properties(vchan-tests PROPERTIES AUTOMOC ON)
    target_link_libraries(vchan-tests PRIVATE Qt6::Core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vchan-tests PRIVATE -Wall -Wextra)
    endif()
    enable_testing()
    add_test(NAME vchan-schedule COMMAND vchan-tests)
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
