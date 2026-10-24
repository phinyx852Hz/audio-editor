# SPDX-License-Identifier: AGPL-3.0-only
#
# Ami Sampler by Finley Baker (_astriid_, GPL-3.0) has only a Projucer project.
# This builds it as a VST3 instrument with PHI's JUCE, with the settings of
# its ami.jucer. Copied into its source folder by cmake/Instruments.cmake.
cmake_minimum_required(VERSION 3.22)
project(AmiSampler VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(JUCE_DIR "" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)

juce_add_plugin(AmiSampler
    PRODUCT_NAME "Ami Sampler"
    COMPANY_NAME "_astriid_"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Xwql
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB images Res/*.png)
juce_add_binary_data(AmiSamplerData SOURCES ${images} Res/amidos.ttf)
juce_generate_juce_header(AmiSampler)
file(GLOB_RECURSE sources Source/*.cpp)
target_sources(AmiSampler PRIVATE ${sources})
target_compile_definitions(AmiSampler PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_WAV_DO_NOT_PAD_HEADER_SIZE=1)
target_link_libraries(AmiSampler PRIVATE AmiSamplerData
    juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
