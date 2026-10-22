# SPDX-License-Identifier: AGPL-3.0-only
#
# Helm by Matt Tytel (GPL-3.0), from amilajack's fork, whose sources are ported
# to JUCE 9 but whose CMake build is for macOS only. This builds the same plugin
# for Linux as a VST3, with the JUCE that fork pins. Copied over its top-level
# CMakeLists.txt by cmake/Instruments.cmake.
cmake_minimum_required(VERSION 3.22)
project(Helm VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 3fb5cbc0340758596dac39f7c8de47d562b4896e)
FetchContent_MakeAvailable(juce)

include(cmake/Sources.cmake)
juce_add_binary_data(HelmResources SOURCES ${HELM_RESOURCES})
juce_add_plugin(HelmPlugin
    COMPANY_NAME "Matt Tytel" BUNDLE_ID org.tytel.helmplugin
    PLUGIN_MANUFACTURER_CODE Tyte PLUGIN_CODE Helm
    FORMATS VST3 PRODUCT_NAME Helm
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(HelmPlugin)
target_sources(HelmPlugin PRIVATE ${HELM_COMMON_SOURCES} ${HELM_PLUGIN_SOURCES})
target_compile_options(HelmPlugin PRIVATE -Wno-deprecated-declarations)
target_include_directories(HelmPlugin PRIVATE
    concurrentqueue mopo/src src src/common src/editor_components
    src/editor_sections src/look_and_feel src/synthesis src/standalone)
# LINUX picks Helm's Linux paths (patches, settings).
target_compile_definitions(HelmPlugin PUBLIC LINUX=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_VST3_CAN_REPLACE_VST2=1 JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)
target_link_libraries(HelmPlugin PRIVATE HelmResources
    juce::juce_audio_utils juce::juce_gui_extra juce::juce_opengl
    PUBLIC juce::juce_recommended_config_flags)
