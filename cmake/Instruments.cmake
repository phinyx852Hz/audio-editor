# SPDX-License-Identifier: AGPL-3.0-only
#
# Stock instruments: free samplers and synths by other authors, built from a
# pinned commit of each one's repository into ${PHINYX_STOCK_DIR} with its
# licence, like the stock plugins (included by cmake/StockPlugins.cmake). They
# are the Sample and Synth menus' instruments, named by what they do:
#
#   Granular           GRNLR by Raffael Seyfried                 GPL-3.0-or-later  VST3
#   Retro Sampler      Ami Sampler by Finley Baker (_astriid_)   GPL-3.0           VST3
#   Wavetable          Vaporizer2 by VAST Dynamics               GPL-3.0           VST3
#   Modular            Cardinal by DISTRHO, with VCV Fundamental GPL-3.0-or-later  CLAP
#   Additive           Overtonium by Benjamin Dehli              AGPL-3.0          VST3
#   Subtractive1 mono  CA-72 by Idle Foundry (a Minimoog Model D) GPL-3.0-or-later CLAP
#   Subtractive2 poly  Helm by Matt Tytel                        GPL-3.0           VST3
#   FM                 OctaSine by Joakim Frostegård             AGPL-3.0          CLAP
#
# GPLv3 and AGPLv3 works may be combined (section 13 of each), so all of them can
# ship with PHINYX. Left out on purpose: Ami's sample packs (their rights are not
# clear) and every Cardinal module collection but its own host modules and VCV
# Fundamental, whose panels are GPL-3.0-or-later or CC0 (others carry
# non-commercial or permission-only artwork). Cardinal's knobs and jacks still
# come from VCV Rack's component library, which is CC-BY-NC-4.0.
#
# Credits are in Source/engine/StockPlugins.cpp and README.md. Each needs what
# its own build needs: JUCE's Linux headers for the JUCE ones (as PHINYX),
# Python 3 and fftw for Cardinal, Rust (cargo) for CA-72 and OctaSine, which
# are left out without it.

option(PHINYX_STOCK_INSTRUMENTS "Build and bundle the stock instruments" ON)
if(NOT PHINYX_STOCK_INSTRUMENTS)
    return()
endif()

set(instruments_dir ${CMAKE_CURRENT_LIST_DIR}/instruments)
# The JUCE that PHINYX itself is built with, for GRNLR and Ami, which bring none.
if(JUCE_DIR)
    set(instrument_juce ${JUCE_DIR})
else()
    set(instrument_juce ${juce_SOURCE_DIR})
endif()

# Builds a JUCE instrument's VST3 with CMake and copies it, and its licence files, in.
#   phinyx_juce_instrument(name repository commit artefact-path licence-files... [CMAKE_ARGS ...] [PATCH ...])
function(phinyx_juce_instrument name repository commit artefact)
    cmake_parse_arguments(PARSE_ARGV 4 arg "" "" "LICENCES;CMAKE_ARGS;PATCH")
    phinyx_stock_licence(${name} ${arg_LICENCES})
    get_filename_component(bundle "${artefact}" NAME)
    ExternalProject_Add(stock_${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG ${commit}
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_COMMAND ""
        PATCH_COMMAND ${arg_PATCH}
        ${stock_one_at_a_time}
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release ${arg_CMAKE_ARGS}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PHINYX_STOCK_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E rm -rf "${PHINYX_STOCK_DIR}/${bundle}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "<BINARY_DIR>/${artefact}" "${PHINYX_STOCK_DIR}/${bundle}"
            ${stock_licence_commands})
    set(stock_targets ${stock_targets} stock_${name} PARENT_SCOPE)
endfunction()

# Granular: GRNLR (a Projucer project for JUCE 4; built by our CMakeLists, made to fit JUCE 8)
phinyx_juce_instrument(GRNLR https://github.com/passivist/GRNLR.git 17702ff3b28fd71cd2a20840120805bc978431c6
    GRNLR_artefacts/Release/VST3/GRNLR.vst3
    LICENCES LICENSE.md
    CMAKE_ARGS -DJUCE_DIR=${instrument_juce}
    PATCH ${CMAKE_COMMAND} -E copy ${instruments_dir}/grnlr/CMakeLists.txt <SOURCE_DIR>/
        COMMAND ${CMAKE_COMMAND} -DPATCH=${instruments_dir}/grnlr/grnlr-juce8.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake)

# Retro Sampler: Ami Sampler (a Projucer project; built by our CMakeLists, its audio formats' writers
# made to fit JUCE 8). Its sample packs are not shipped.
phinyx_juce_instrument(AmiSampler https://github.com/astriiddev/Ami-Sampler-VST.git 6d76a29c9096ca80c2fb77d35afc77ceb329c05e
    "AmiSampler_artefacts/Release/VST3/Ami Sampler.vst3"
    LICENCES LICENSE.txt
    CMAKE_ARGS -DJUCE_DIR=${instrument_juce}
    PATCH ${CMAKE_COMMAND} -E copy ${instruments_dir}/ami/CMakeLists.txt <SOURCE_DIR>/
        COMMAND ${CMAKE_COMMAND} -DPATCH=${instruments_dir}/ami/ami-juce8.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake)

# Wavetable: Vaporizer2, with its own JUCE. Its wavetables, noises and presets go
# into the bundle's Resources folder, where the patch makes it look on Linux.
phinyx_juce_instrument(Vaporizer2 https://github.com/VASTDynamics/Vaporizer2.git 1c56c4be304255f1c397dad725ea784f15a55ef5
    VASTvaporizer2_artefacts/Release/VST3/VASTvaporizer2.vst3
    LICENCES LICENSE
    PATCH ${CMAKE_COMMAND} -DPATCH=${instruments_dir}/vaporizer2-bundle-data.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake)
ExternalProject_Add_Step(stock_Vaporizer2 data
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PHINYX_STOCK_DIR}/VASTvaporizer2.vst3/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/VASTvaporizer/Noises "${PHINYX_STOCK_DIR}/VASTvaporizer2.vst3/Contents/Resources/Noises"
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/VASTvaporizer/Tables "${PHINYX_STOCK_DIR}/VASTvaporizer2.vst3/Contents/Resources/Tables"
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/VASTvaporizer/Presets "${PHINYX_STOCK_DIR}/VASTvaporizer2.vst3/Contents/Resources/Presets"
    DEPENDEES install)

# Additive: Overtonium, with the JUCE it pins
phinyx_juce_instrument(Overtonium https://github.com/benjamindehli/overtonium.git 894224746018c4d7c9bf28d468bc2c99637fddc2
    Overtonium_artefacts/Release/VST3/Overtonium.vst3
    LICENCES LICENSE
    CMAKE_ARGS -DOVERTONIUM_INSTALL_AFTER_BUILD=OFF -DOVERTONIUM_BUILD_TESTS=OFF -DOVERTONIUM_BUILD_TOOLS=OFF
        -DOVERTONIUM_WARNINGS_AS_ERRORS=OFF)

# Subtractive2 poly: Helm (amilajack's fork, ported to JUCE 9; its own CMake build is
# for macOS, so ours builds the same sources for Linux). Its factory patches go
# into the bundle's Resources folder, where the patch makes it look on Linux.
phinyx_juce_instrument(Helm https://github.com/amilajack/helm.git ce20c330364753a13f119638d84c469e5b9aae36
    HelmPlugin_artefacts/Release/VST3/Helm.vst3
    LICENCES COPYING
    PATCH ${CMAKE_COMMAND} -E copy ${instruments_dir}/helm/CMakeLists.txt <SOURCE_DIR>/
        COMMAND ${CMAKE_COMMAND} -DPATCH=${instruments_dir}/helm/helm-linux.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake)
ExternalProject_Add_Step(stock_Helm patches
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/patches "${PHINYX_STOCK_DIR}/Helm.vst3/Contents/Resources/patches"
    DEPENDEES install)

# Modular: Cardinal, cut down to its own host modules and VCV Fundamental
# (cardinal_foundational.py); only those module folders are checked out. Built
# as CLAPs; the synth one is used.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    phinyx_stock_licence(Cardinal LICENSE docs/LICENSES.md plugins/Fundamental/LICENSE)
    ExternalProject_Add(stock_Cardinal
        GIT_REPOSITORY https://github.com/DISTRHO/Cardinal.git
        GIT_TAG 0a530b73273afc914ec71a78e9165cd28c53b599
        GIT_SUBMODULES src/Rack dpf carla deps/QuickJS plugins/Fundamental
        GIT_SUBMODULES_RECURSE TRUE
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        PATCH_COMMAND ${Python3_EXECUTABLE} -I ${instruments_dir}/cardinal_foundational.py ${instruments_dir}/cardinal-plugins.cpp
        ${stock_one_at_a_time}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${stock_make} clap
        INSTALL_COMMAND ${CMAKE_COMMAND} -E rm -rf ${PHINYX_STOCK_DIR}/Cardinal.clap
            # Its resources are symlinks into the source folder: copied as the files they point to.
            COMMAND cp -rL <SOURCE_DIR>/bin/Cardinal.clap ${PHINYX_STOCK_DIR}/
            COMMAND ${CMAKE_COMMAND} -E rm -f ${PHINYX_STOCK_DIR}/Cardinal.clap/CardinalFX.clap
            ${stock_licence_commands})
    list(APPEND stock_targets stock_Cardinal)
else()
    message(WARNING "Python 3 not found: the Modular instrument (Cardinal) is left out")
endif()

# Subtractive1 mono: CA-72, and FM: OctaSine, Rust plugins bundled as CLAPs by their own xtasks.
find_program(PHINYX_CARGO cargo)
if(PHINYX_CARGO)
    phinyx_stock_licence(CA-72 LICENSE THIRD-PARTY-NOTICES.txt)
    ExternalProject_Add(stock_CA-72
        GIT_REPOSITORY https://github.com/idlefoundry/ca-72.git
        GIT_TAG 7b8f0ee756f576377c273beabe46bc11f5a7f856
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        ${stock_one_at_a_time}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${PHINYX_CARGO} xtask bundle ca72-plugin --profile bundle
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/target/bundled/CA-72.clap ${PHINYX_STOCK_DIR}/
            ${stock_licence_commands})

    phinyx_stock_licence(OctaSine LICENSE)
    ExternalProject_Add(stock_OctaSine
        GIT_REPOSITORY https://github.com/greatest-ape/OctaSine.git
        GIT_TAG e347a9334a3a976029746719afa0bf0875181b20
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        ${stock_one_at_a_time}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${PHINYX_CARGO} xtask bundle -p octasine --release --features clap
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/target/bundled/octasine.clap ${PHINYX_STOCK_DIR}/
            ${stock_licence_commands})
    list(APPEND stock_targets stock_CA-72 stock_OctaSine)
else()
    message(WARNING "Rust (cargo) not found: Subtractive1 mono (CA-72) and FM (OctaSine) are left out")
endif()
