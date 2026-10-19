# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)

project(Phinyx VERSION 0.5.0 LANGUAGES C CXX)

# The app's names live here so a rename touches one place.
set(APP_ID "phinyx" CACHE STRING "Lowercase id: binary, config folder, Wayland app-id")
set(APP_DISPLAY_NAME "PHINYX" CACHE STRING "Name shown to users")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "${APP_DISPLAY_NAME} targets Linux only.")
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent)

# JUCE provides the audio engine only (devices, codecs, DSP, plugin hosting); no JUCE UI.
# Use a local checkout (needed for offline package builds) or fetch the pinned tag.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
# Patched: a VST3's inactive buses get silent buffers instead of null pointers,
# which crashed yabridge-bridged plugins that have a side chain.
set(juce_patch ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/juce-vst3-inactive-buses.patch)
if(JUCE_DIR)
    execute_process(COMMAND ${CMAKE_COMMAND} -DPATCH=${juce_patch} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake
                    WORKING_DIRECTORY ${JUCE_DIR})
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${juce_patch} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake)
    FetchContent_MakeAvailable(juce)
endif()

# A JUCE console target lets us supply our own main() and run the Qt event loop.
juce_add_console_app(${APP_ID}
    PRODUCT_NAME "${APP_ID}"
    VERSION ${PROJECT_VERSION})

target_sources(${APP_ID} PRIVATE
    Source/main.cpp
    Source/core/AudioData.cpp
    Source/core/AudioDocument.cpp
    Source/core/Dsp.cpp
    Source/core/Ixml.cpp
    Source/core/Metadata.cpp
    Source/core/MiniXml.cpp
    Source/core/Mp4Xmp.cpp
    Source/core/RiffChunks.cpp
    Source/core/Xmp.cpp
    Source/core/Repair.cpp
    Source/core/Restoration.cpp
    Source/core/Fft.cpp
    Source/core/SpectrumView.cpp
    Source/core/Generate.cpp
    Source/core/Pitch.cpp
    Source/core/PitchFix.cpp
    Source/core/Binaural.cpp
    Source/core/Ambisonics.cpp
    Source/core/Nyquist.cpp
    Source/core/Album.cpp
    Source/engine/AudioCapture.cpp
    Source/engine/AudioEngine.cpp
    Source/engine/BuiltinEffects.cpp
    Source/engine/ClapFormat.cpp
    Source/engine/Codec.cpp
    Source/engine/Export.cpp
    Source/engine/Batch.cpp
    Source/engine/TagIO.cpp
    Source/engine/MasterChain.cpp
    Source/engine/StockFrame.cpp
    Source/engine/StockPlugins.cpp
    Source/engine/VampHost.cpp
    Source/engine/X11Windows.cpp
    Source/ui/CommandRegistry.cpp
    Source/ui/Dialogs.cpp
    Source/ui/ExportDialog.cpp
    Source/ui/MetadataEditor.cpp
    Source/ui/MainWindow.cpp
    Source/ui/GainHud.cpp
    Source/ui/LiveEdits.cpp
    Source/ui/Analysis.cpp
    Source/ui/ReferencePanel.cpp
    Source/ui/ReferenceSection.cpp
    Source/ui/ExportSection.cpp
    Source/ui/BatchWindow.cpp
    Source/ui/AlbumEditor.cpp
    Source/ui/AlbumFile.cpp
    Source/ui/AlbumView.cpp
    Source/ui/ChainRow.cpp
    Source/ui/DesktopShortcuts.cpp
    Source/ui/MasterPanel.cpp
    Source/ui/Panels.cpp
    Source/ui/RecordDialog.cpp
    Source/ui/Recovery.cpp
    Source/ui/Spectrogram.cpp
    Source/ui/Theme.cpp
    Source/ui/TransportBar.cpp
    Source/ui/InfoStrip.cpp
    Source/ui/TimeRuler.cpp
    Source/ui/Overview.cpp
    Source/ui/FindPanel.cpp
    Source/ui/Goniometer.cpp
    Source/ui/NyquistUi.cpp
    Source/ui/WaveformView.cpp
    Source/ui/SpectrogramDialog.cpp
    Source/ui/GenerateSection.cpp
    Source/ui/PitchSection.cpp
    Source/ui/MacroSection.cpp
    Source/ui/Macros.cpp
    Source/ui/ToolsSection.cpp
    Source/ui/AmbiDialogs.cpp
    Source/ui/AmbiScene.cpp
    Source/ui/BinauralDialog.cpp
    Source/ui/SpatialSection.cpp)

# CLAP plugin hosting needs only the CLAP headers: a local checkout, the
# system's (clap/clap.h, e.g. the clap package), or the pinned release.
set(CLAP_DIR "" CACHE PATH "Path to a local CLAP SDK checkout (the folder holding include/clap)")
if(CLAP_DIR)
    set(PHINYX_CLAP_INCLUDE "${CLAP_DIR}/include")
else()
    find_path(PHINYX_CLAP_INCLUDE clap/clap.h)
    if(NOT PHINYX_CLAP_INCLUDE)
        include(FetchContent)
        FetchContent_Declare(clap
            GIT_REPOSITORY https://github.com/free-audio/clap.git
            GIT_TAG 1.2.7
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(clap)
        if(NOT clap_POPULATED)
            FetchContent_Populate(clap)
        endif()
        set(PHINYX_CLAP_INCLUDE "${clap_SOURCE_DIR}/include")
    endif()
endif()
target_include_directories(${APP_ID} SYSTEM PRIVATE ${PHINYX_CLAP_INCLUDE})

# JACK headers come from jack2 or pipewire-jack. JUCE loads libjack at run time,
# so JACK output works with either; without headers PHINYX builds ALSA-only.
include(CheckIncludeFile)
check_include_file(jack/jack.h PHINYX_HAVE_JACK_HEADERS)
if(PHINYX_HAVE_JACK_HEADERS)
    set(PHINYX_JACK 1)
else()
    set(PHINYX_JACK 0)
    message(STATUS "jack/jack.h not found: building without the JACK backend (ALSA and PipeWire's ALSA still work)")
endif()

# Recording (desktop audio, inputs, single apps) talks to PipeWire directly.
# Without libpipewire-0.3 PHINYX still builds; it just cannot record.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
endif()
if(PIPEWIRE_FOUND)
    set(PHINYX_PIPEWIRE 1)
    target_include_directories(${APP_ID} SYSTEM PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
    target_link_libraries(${APP_ID} PRIVATE PkgConfig::PIPEWIRE)
else()
    set(PHINYX_PIPEWIRE 0)
    message(STATUS "libpipewire-0.3 not found: building without recording")
endif()

# Export and import of compressed formats (FFmpeg) and their tags (TagLib).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib>=1.12)
target_link_libraries(${APP_ID} PRIVATE PkgConfig::FFMPEG PkgConfig::TAGLIB)

target_compile_definitions(${APP_ID} PRIVATE
    PHINYX_PIPEWIRE=${PHINYX_PIPEWIRE}
    APP_ID="${APP_ID}"
    APP_DISPLAY_NAME="${APP_DISPLAY_NAME}"
    APP_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ALSA=1
    JUCE_JACK=${PHINYX_JACK}
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=1)

target_link_libraries(${APP_ID}
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

include(GNUInstallDirs)

# Vamp analysis plugins (cmake/Vamp.cmake)
include(cmake/Vamp.cmake)
target_link_libraries(${APP_ID} PRIVATE phinyx_vamp_host)

# Rubber Band (cmake/RubberBand.cmake): pitch correction.
include(cmake/RubberBand.cmake)
target_link_libraries(${APP_ID} PRIVATE phinyx_rubberband)

# libmysofa (cmake/Mysofa.cmake): HRTF sets for binaural rendering, with the MIT KEMAR set.
include(cmake/Mysofa.cmake)
target_link_libraries(${APP_ID} PRIVATE phinyx_mysofa)
target_compile_definitions(${APP_ID} PRIVATE PHINYX_DEFAULT_HRTF="${PHINYX_DEFAULT_HRTF}")
install(FILES "${PHINYX_DEFAULT_HRTF}" DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_ID}/hrtf")

# Nyquist effects (cmake/Nyquist.cmake): libnyquist, its runtime and the bundled plug-ins.
include(cmake/Nyquist.cmake)
target_link_libraries(${APP_ID} PRIVATE phinyx_nyquist)
file(RELATIVE_PATH nyquist_relative "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_DATADIR}/${APP_ID}")
target_compile_definitions(${APP_ID} PRIVATE
    PHINYX_SHARE_RELATIVE="${nyquist_relative}"
    PHINYX_SHARE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/share")

# Stock plugins (cmake/StockPlugins.cmake). Off builds PHINYX without them.
option(PHINYX_STOCK_PLUGINS "Build and bundle the stock plugins" ON)
if(PHINYX_STOCK_PLUGINS)
    include(cmake/StockPlugins.cmake)
else()
    set(PHINYX_STOCK_DIR "${CMAKE_BINARY_DIR}/stock")
    set(PHINYX_STOCK_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/${APP_ID}/stock")
endif()
file(RELATIVE_PATH stock_relative "/${CMAKE_INSTALL_BINDIR}" "/${PHINYX_STOCK_INSTALL_DIR}")
target_compile_definitions(${APP_ID} PRIVATE
    PHINYX_STOCK_RELATIVE="${stock_relative}"
    PHINYX_STOCK_BUILD_DIR="${PHINYX_STOCK_DIR}")

install(TARGETS ${APP_ID} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/phinyx.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Core tests: plain C++, no JUCE or Qt. Run with ctest.
option(PHINYX_BUILD_TESTS "Build the core tests" ON)
if(PHINYX_BUILD_TESTS)
    enable_testing()
    add_executable(phinyx-core-tests
        tests/CoreTests.cpp
        Source/core/AudioData.cpp
        Source/core/AudioDocument.cpp
        Source/core/Dsp.cpp
        Source/core/Ixml.cpp
        Source/core/Metadata.cpp
        Source/core/MiniXml.cpp
        Source/core/RiffChunks.cpp
        Source/core/Xmp.cpp
        Source/core/Repair.cpp
        Source/core/Restoration.cpp
        Source/core/Fft.cpp
        Source/core/SpectrumView.cpp
        Source/core/Generate.cpp
        Source/core/Pitch.cpp
        Source/core/PitchFix.cpp
    Source/core/Binaural.cpp
    Source/core/Ambisonics.cpp
        Source/core/Nyquist.cpp
        Source/core/Album.cpp)
    target_link_libraries(phinyx-core-tests PRIVATE phinyx_nyquist phinyx_rubberband phinyx_mysofa)
    target_compile_definitions(phinyx-core-tests PRIVATE PHINYX_DEFAULT_HRTF="${PHINYX_DEFAULT_HRTF}")
    target_compile_definitions(phinyx-core-tests PRIVATE PHINYX_SHARE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/share")
    add_test(NAME core COMMAND phinyx-core-tests)
endif()
