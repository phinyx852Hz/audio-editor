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
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
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
    Source/core/Fft.cpp
    Source/engine/AudioEngine.cpp
    Source/engine/MasterChain.cpp
    Source/ui/CommandRegistry.cpp
    Source/ui/Dialogs.cpp
    Source/ui/MainWindow.cpp
    Source/ui/ChainRow.cpp
    Source/ui/DesktopShortcuts.cpp
    Source/ui/MasterPanel.cpp
    Source/ui/Panels.cpp
    Source/ui/Spectrogram.cpp
    Source/ui/Theme.cpp
    Source/ui/WaveformView.cpp)

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

target_compile_definitions(${APP_ID} PRIVATE
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
        Source/core/Fft.cpp)
    add_test(NAME core COMMAND phinyx-core-tests)
endif()
