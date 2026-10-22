# SPDX-License-Identifier: AGPL-3.0-only
#
# Tracker2: Furnace by tildearrow (GPL-2.0-or-later), the chiptune tracker, built
# from a pinned release into ${PHINYX_STOCK_DIR}/Furnace with its licence, like the
# stock instruments (included by cmake/StockPlugins.cmake). It runs as a program of
# its own in its own window (Tracker > Tracker2); the patch adds an opacity bar along
# the bottom of that window, like the instruments' windows have. Tracker1 is PHINYX's
# own, built on MilkyTracker's MilkyPlay (third_party/milkyplay).
#
# Its libraries (SDL2, fmt, FreeType, libsndfile, PortAudio, RtMidi, zlib, FFTW) are
# its own vendored copies; SDL2 needs the X11 headers, and ALSA's and JACK's (or
# PipeWire's JACK) for sound. Its demo songs are left out (they are not under the GPL).

option(PHINYX_STOCK_TRACKERS "Build and bundle Tracker2 (Furnace)" ON)
if(NOT PHINYX_STOCK_INSTRUMENTS OR NOT PHINYX_STOCK_TRACKERS)
    return()
endif()

phinyx_stock_licence(Furnace LICENSE)
ExternalProject_Add(stock_Furnace
    GIT_REPOSITORY https://github.com/tildearrow/furnace.git
    GIT_TAG f2619dbd988c92bf9c7173648d191648f60aecbf   # v0.6.8.3
    GIT_SUBMODULES_RECURSE TRUE
    UPDATE_COMMAND ""
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_LIST_DIR}/trackers/furnace-opacity-bar.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake
    ${stock_one_at_a_time}
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DWITH_DEMOS=OFF -DCMAKE_INSTALL_PREFIX=${PHINYX_STOCK_DIR}/Furnace
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PHINYX_STOCK_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} -E rm -rf ${PHINYX_STOCK_DIR}/Furnace
        COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --strip
        ${stock_licence_commands})
list(APPEND stock_targets stock_Furnace)
