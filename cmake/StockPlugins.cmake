# SPDX-License-Identifier: AGPL-3.0-only
#
# Stock plugins: free plugins by other authors that ship with PHI and are
# listed under Built-in. Each is built from a pinned commit of its own
# repository as a CLAP (or, for the JUCE ones, a VST3) and copied, with its
# licence, into ${PHI_STOCK_DIR}. Their licences (AGPLv3, GPLv3, LGPLv3,
# BSD-3-Clause, MIT) all allow this; credits are in Source/engine/StockPlugins.cpp
# and README.md.
#
# Needs make, git, and the headers they use: cairo, libsndfile, X11, Xrandr,
# Xcursor, Xext and OpenGL. The ZL Audio plugins also need Clang 17 or later
# and freetype and fontconfig; without Clang they are left out.

include(ExternalProject)

set(PHI_STOCK_DIR "${CMAKE_BINARY_DIR}/stock")
set(PHI_STOCK_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/${APP_ID}/stock")
file(MAKE_DIRECTORY "${PHI_STOCK_DIR}/licenses")

# The stock plugins build after PHI itself, one at a time (Ninja's console
# pool, via USES_TERMINAL_*), each with a job count that fits in memory: their
# C++ can need up to about 3 GB per compiler process, so building them all at
# once with a job per core ran a 30 GB machine out of memory.
cmake_host_system_information(RESULT stock_cores QUERY NUMBER_OF_LOGICAL_CORES)
cmake_host_system_information(RESULT stock_memory QUERY TOTAL_PHYSICAL_MEMORY)   # MiB
math(EXPR stock_jobs_default "${stock_memory} / 3072")
if(stock_jobs_default GREATER stock_cores)
    set(stock_jobs_default ${stock_cores})
endif()
if(stock_jobs_default LESS 1)
    set(stock_jobs_default 1)
endif()
set(PHI_STOCK_JOBS ${stock_jobs_default} CACHE STRING "Parallel jobs for building each stock plugin")
set(stock_make make -j${PHI_STOCK_JOBS})
set(stock_one_at_a_time USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE USES_TERMINAL_INSTALL TRUE)

function(phi_stock_licence name)
    # Remaining arguments: licence files, relative to the source folder
    set(commands "")
    foreach(f IN LISTS ARGN)
        string(REPLACE "/" "-" target "${f}")   # libxputty/LICENSE -> libxputty-LICENSE
        list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/${f} ${PHI_STOCK_DIR}/licenses/${name}/${target})
    endforeach()
    set(stock_licence_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PHI_STOCK_DIR}/licenses/${name}
        ${commands} PARENT_SCOPE)
endfunction()

# ToneShiftEQ by brummer10 (BSD-3-Clause; its libxputty GUI library is 0BSD)
phi_stock_licence(ToneShiftEQ LICENSE libxputty/LICENSE)
ExternalProject_Add(stock_toneshifteq
    GIT_REPOSITORY https://github.com/brummer10/ToneShiftEQ.git
    GIT_TAG 64f4d7f2f704cbd2f90c5733d5dabc835061f45b
    GIT_SUBMODULES_RECURSE TRUE
    BUILD_IN_SOURCE TRUE
    UPDATE_COMMAND ""
    ${stock_one_at_a_time}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${stock_make} clap
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bin/toneshifteq.clap ${PHI_STOCK_DIR}/
        ${stock_licence_commands})

# Wolf Shaper by Patrick Desaulniers (GPLv3), on DPF (ISC)
phi_stock_licence(WolfShaper LICENSE dpf/LICENSE)
ExternalProject_Add(stock_wolfshaper
    GIT_REPOSITORY https://github.com/wolf-plugins/wolf-shaper.git
    GIT_TAG d38cc33d482de21ecc76d9b817ec5f8dea1fb5fb
    GIT_SUBMODULES_RECURSE TRUE
    BUILD_IN_SOURCE TRUE
    UPDATE_COMMAND ""
    ${stock_one_at_a_time}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${stock_make} libs COMMAND ${stock_make} -C src clap
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bin/wolf-shaper.clap ${PHI_STOCK_DIR}/
        ${stock_licence_commands})

# LSP Clipper, LSP Multiband Clipper and LSP Oscilloscope by Vladimir Sadovnikov (LGPL-3.0-or-later).
# `make fetch` checks out the LSP libraries at the versions each release names.
set(lsp_commit_clipper 5177474d5f2c7b90ccaa654a4a90746f0a83a9ef)
set(lsp_commit_mb-clipper 7579c7025d35b390c5240b9bf06c63b3901e262e)
set(lsp_commit_oscilloscope b6a130069b35f81bc3bcebc32af7d687d9580e5d)
foreach(lsp clipper mb-clipper oscilloscope)
    set(commit ${lsp_commit_${lsp}})
    phi_stock_licence(lsp-plugins-${lsp} COPYING COPYING.LESSER)
    ExternalProject_Add(stock_lsp_${lsp}
        GIT_REPOSITORY https://github.com/lsp-plugins/lsp-plugins-${lsp}.git
        GIT_TAG ${commit}
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        ${stock_one_at_a_time}
        CONFIGURE_COMMAND make config "FEATURES=clap ui" PREFIX=/usr COMMAND make fetch
        BUILD_COMMAND ${stock_make}
        INSTALL_COMMAND make install DESTDIR=<BINARY_DIR>/staging
            COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/staging/usr/lib/clap/lsp-plugins-${lsp}.clap ${PHI_STOCK_DIR}/
            ${stock_licence_commands})
endforeach()

set(stock_targets stock_toneshifteq stock_wolfshaper stock_lsp_clipper stock_lsp_mb-clipper stock_lsp_oscilloscope)

# QM Vamp Plugins by the Centre for Digital Music, Queen Mary, University of
# London (GPL-2.0-or-later): beat, bar, onset, tempo and key detection and more,
# for the Analyze menu. Built with the qm-dsp library and Vamp SDK at the
# commits its repoint-lock.json names; installed into the stock vamp folder.
# Their makefiles use x86 SSE flags, so other CPUs go without them for now.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(qm_makefile build/linux/Makefile.linux64)
    phi_stock_licence(qm-vamp-plugins COPYING)
    ExternalProject_Add(stock_qm_vamp
        GIT_REPOSITORY https://github.com/c4dm/qm-vamp-plugins.git
        GIT_TAG c918afc070f79489758bb2dab6895972bd82066b
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        ${stock_one_at_a_time}
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E rm -rf lib
            COMMAND git clone -q https://github.com/c4dm/qm-dsp.git lib/qm-dsp
            COMMAND git -C lib/qm-dsp checkout -q e34a3cc188332ed7c33cd9257ef164de5b587191
            COMMAND git clone -q https://github.com/c4dm/vamp-plugin-sdk.git lib/vamp-plugin-sdk
            COMMAND git -C lib/vamp-plugin-sdk checkout -q c3ef877327f1b081b95a1d05349c297f8c4454f0
        # qm-dsp first: the plugin's makefile builds it as a phony prerequisite, which races under -j.
        BUILD_COMMAND ${stock_make} -C lib/qm-dsp -f ${qm_makefile}
            COMMAND ${stock_make} -f ${qm_makefile}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${PHI_STOCK_DIR}/vamp
            COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/qm-vamp-plugins.so <SOURCE_DIR>/qm-vamp-plugins.cat
                <SOURCE_DIR>/qm-vamp-plugins.n3 ${PHI_STOCK_DIR}/vamp/
            ${stock_licence_commands})
    list(APPEND stock_targets stock_qm_vamp)
else()
    message(STATUS "QM Vamp Plugins are built on x86-64 only: left out")
endif()

# The ZL Audio plugins by zsliu98 (AGPL-3.0, like PHI), built with their own
# JUCE fork as VST3s. Their logos are excluded from the licence and are not
# shown by PHI. They use Highway with one fixed SIMD level: SSE4 runs on any
# x86-64 CPU from 2009 on; AVX2 is faster on 2013 and later ones.
find_program(PHI_ZL_CLANG NAMES clang++-20 clang++-19 clang++-18 clang++-17 clang++)
if(PHI_ZL_CLANG)
    execute_process(COMMAND ${PHI_ZL_CLANG} -dumpversion OUTPUT_VARIABLE zl_clang_version OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT PHI_ZL_CLANG OR zl_clang_version VERSION_LESS 17)
    message(WARNING "Clang 17 or later not found: the ZL Audio stock plugins are left out")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(zl_simd_default NEON)
    else()
        set(zl_simd_default SSE4)
    endif()
    set(PHI_ZL_SIMD ${zl_simd_default} CACHE STRING "SIMD level for the ZL Audio plugins: SSE2, SSE4, AVX2 or NEON")
    string(REGEX REPLACE "\\+\\+(-[0-9]+)?$" "\\1" zl_cc "${PHI_ZL_CLANG}")   # clang++-18 -> clang-18

    # name, repository, commit, product name (the .vst3 bundle's name)
    set(zl_plugins
        "ZLEqualizer|a518319f7e388c56f1968fbd3ad62424142ec709|ZL Equalizer 2"
        "ZLCompressor|c11049e86e5bc53c8897041895924218fe3b357a|ZL Compressor"
        "ZLLimiter|ef154835e931c31dd76410e75ac0c7faeec65937|ZL Limiter"
        "ZLSplitter|a8f31f6251933ff2e3366dde33939a68be5c0e71|ZL Splitter"
        "ZLSpectrumEqualizer|39f2abae17782ed4a16b09d074e0c2920b0c6d71|ZL Spectrum Equalizer")
    foreach(entry IN LISTS zl_plugins)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 zl_name)
        list(GET parts 1 zl_commit)
        list(GET parts 2 zl_product)
        phi_stock_licence(${zl_name} LICENSE.md)   # plus its licenses folder (JUCE, Highway, fonts...)
        ExternalProject_Add(stock_${zl_name}
            GIT_REPOSITORY https://github.com/ZL-Audio/${zl_name}.git
            GIT_TAG ${zl_commit}
            GIT_SUBMODULES_RECURSE TRUE
            UPDATE_COMMAND ""
            ${stock_one_at_a_time}
            CMAKE_GENERATOR Ninja
            CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_C_COMPILER=${zl_cc} -DCMAKE_CXX_COMPILER=${PHI_ZL_CLANG}
                -DZL_JUCE_FORMATS=VST3 -DZL_JUCE_COPY_PLUGIN=FALSE -DZL_HWY_STATIC_TARGET=${PHI_ZL_SIMD}
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PHI_STOCK_JOBS}
            INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
                "<BINARY_DIR>/${zl_name}_artefacts/Release/VST3/${zl_product}.vst3" "${PHI_STOCK_DIR}/${zl_product}.vst3"
                ${stock_licence_commands}
                COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/licenses ${PHI_STOCK_DIR}/licenses/${zl_name})
        list(APPEND stock_targets stock_${zl_name})
    endforeach()
endif()

# Airwindows Consolidated: every Airwindows effect by Chris Johnson (MIT) in one
# plugin, by Paul Walker (MIT), as a JUCE VST3 (so the build is GPL-3.0, with
# JUCE). Patched (cmake/airwindows/consolidated-phi.patch) so its background takes
# the PHI frame's colour, the rest of its view as it is, and to pin
# clap-juce-extensions, which its build fetches. Its own build brings its JUCE.
# Its airwindows and Rack submodules are only for regenerating its sources and
# for the VCV Rack module, so they are left out.
set(airwindows_dir ${CMAKE_CURRENT_LIST_DIR}/airwindows)
phi_stock_licence(AirwindowsConsolidated LICENSE.md res/LICENSE.gpl3 res/OFL.txt)
ExternalProject_Add(stock_airwindows
    GIT_REPOSITORY https://github.com/baconpaul/airwin2rack.git
    GIT_TAG d8fe3010bc6dce91e30ee2f2ef15dd3683533a97
    GIT_SUBMODULES ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${airwindows_dir}/consolidated-phi.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake
    ${stock_one_at_a_time}
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_JUCE_PLUGIN=TRUE
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target airwin-consolidated_VST3 --parallel ${PHI_STOCK_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} -E rm -rf "${PHI_STOCK_DIR}/Airwindows Consolidated.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "<BINARY_DIR>/src-juce/airwin-consolidated_artefacts/Release/VST3/Airwindows Consolidated.vst3"
            "${PHI_STOCK_DIR}/Airwindows Consolidated.vst3"
        ${stock_licence_commands}
        COMMAND ${CMAKE_COMMAND} -E copy ${airwindows_dir}/airwindows-LICENSE ${PHI_STOCK_DIR}/licenses/AirwindowsConsolidated/airwindows-LICENSE)
list(APPEND stock_targets stock_airwindows)

include(${CMAKE_CURRENT_LIST_DIR}/Instruments.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Trackers.cmake)

foreach(t IN LISTS stock_targets)
    add_dependencies(${t} ${APP_ID})
endforeach()
add_custom_target(stock_plugins ALL DEPENDS ${stock_targets})

install(DIRECTORY "${PHI_STOCK_DIR}/" DESTINATION "${PHI_STOCK_INSTALL_DIR}" USE_SOURCE_PERMISSIONS)
