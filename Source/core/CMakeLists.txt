# The DSP of the Φ effects, the Φ Phaser and the start-up voice: plain C++ (no JUCE
# or Qt), as one library that the PHI synth links too. From another project:
#   FetchContent_Declare(phi_dsp GIT_REPOSITORY <this repository> SOURCE_SUBDIR Source/core)
#   FetchContent_MakeAvailable(phi_dsp)
#   target_link_libraries(<synth> PRIVATE phi_dsp)
# and include "Phi.h", "PhiPhaser.h" or "PhiVoice.h".
cmake_minimum_required(VERSION 3.22)
project(phi_dsp LANGUAGES CXX)

add_library(phi_dsp STATIC
    Phi.cpp
    PhiPhaser.cpp
    PhiVoice.cpp
    Fft.cpp)
target_include_directories(phi_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(phi_dsp PUBLIC cxx_std_20)
set_target_properties(phi_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)   # for plugins (CLAP, VST3) too
