# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)

project(AudioEditor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This application targets Linux only.")
endif()

# Use a local JUCE checkout (needed for offline Flatpak builds) or fetch the pinned tag.
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

juce_add_gui_app(AudioEditor
    PRODUCT_NAME "Audio Editor"
    COMPANY_NAME "Audio Editor Project"
    VERSION ${PROJECT_VERSION})

target_sources(AudioEditor PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/WaveformView.cpp
    Source/CommandPalette.cpp)

target_compile_definitions(AudioEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ALSA=1
    JUCE_JACK=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AudioEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AudioEditor,JUCE_VERSION>")

target_link_libraries(AudioEditor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

install(TARGETS AudioEditor RUNTIME DESTINATION bin)
install(FILES packaging/audio-editor.desktop DESTINATION share/applications)
