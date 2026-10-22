# SPDX-License-Identifier: AGPL-3.0-only
#
# GRNLR by Raffael Seyfried (GPL-3.0-or-later) has only a Projucer project for
# JUCE 4. This builds it as a VST3 instrument with PHINYX's JUCE; the changes it
# needs for today's JUCE are in grnlr-juce8.patch. Copied into its source folder
# by cmake/Instruments.cmake.
cmake_minimum_required(VERSION 3.22)
project(GRNLR VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(JUCE_DIR "" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)

juce_add_plugin(GRNLR
    PRODUCT_NAME "GRNLR"
    COMPANY_NAME "Raffael Seyfried"
    PLUGIN_MANUFACTURER_CODE RaSy
    PLUGIN_CODE Grnl
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(GRNLR)
target_sources(GRNLR PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(GRNLR PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 DONT_SET_USING_JUCE_NAMESPACE=0)
target_link_libraries(GRNLR PRIVATE
    juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
