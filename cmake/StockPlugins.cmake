# SPDX-License-Identifier: AGPL-3.0-only
#
# Stock plugins: free plugins by other authors that ship with PHINYX and are
# listed under Built-in. Each is built from a pinned commit of its own
# repository as a CLAP and copied, with its licence, into ${PHINYX_STOCK_DIR}.
# Their licences (GPLv3, LGPLv3, BSD-3-Clause) all allow this; credits are in
# Source/engine/StockPlugins.cpp and README.md.
#
# Needs make, git, and the headers they use: cairo, libsndfile, X11, Xrandr,
# Xcursor, Xext and OpenGL.

include(ExternalProject)

set(PHINYX_STOCK_DIR "${CMAKE_BINARY_DIR}/stock")
set(PHINYX_STOCK_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/${APP_ID}/stock")
file(MAKE_DIRECTORY "${PHINYX_STOCK_DIR}/licenses")

# Build steps run make in each source folder.
set(stock_make make -j8)

function(phinyx_stock_licence name)
    # Remaining arguments: licence files, relative to the source folder
    set(commands "")
    foreach(f IN LISTS ARGN)
        string(REPLACE "/" "-" target "${f}")   # libxputty/LICENSE -> libxputty-LICENSE
        list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/${f} ${PHINYX_STOCK_DIR}/licenses/${name}/${target})
    endforeach()
    set(stock_licence_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PHINYX_STOCK_DIR}/licenses/${name}
        ${commands} PARENT_SCOPE)
endfunction()

# ToneShiftEQ by brummer10 (BSD-3-Clause; its libxputty GUI library is 0BSD)
phinyx_stock_licence(ToneShiftEQ LICENSE libxputty/LICENSE)
ExternalProject_Add(stock_toneshifteq
    GIT_REPOSITORY https://github.com/brummer10/ToneShiftEQ.git
    GIT_TAG 64f4d7f2f704cbd2f90c5733d5dabc835061f45b
    GIT_SUBMODULES_RECURSE TRUE
    BUILD_IN_SOURCE TRUE
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${stock_make} clap
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bin/toneshifteq.clap ${PHINYX_STOCK_DIR}/
        ${stock_licence_commands})

# Wolf Shaper by Patrick Desaulniers (GPLv3), on DPF (ISC)
phinyx_stock_licence(WolfShaper LICENSE dpf/LICENSE)
ExternalProject_Add(stock_wolfshaper
    GIT_REPOSITORY https://github.com/wolf-plugins/wolf-shaper.git
    GIT_TAG d38cc33d482de21ecc76d9b817ec5f8dea1fb5fb
    GIT_SUBMODULES_RECURSE TRUE
    BUILD_IN_SOURCE TRUE
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${stock_make} libs COMMAND ${stock_make} -C src clap
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bin/wolf-shaper.clap ${PHINYX_STOCK_DIR}/
        ${stock_licence_commands})

# LSP Clipper, LSP Multiband Clipper and LSP Oscilloscope by Vladimir Sadovnikov (LGPL-3.0-or-later).
# `make fetch` checks out the LSP libraries at the versions each release names.
set(lsp_commit_clipper 5177474d5f2c7b90ccaa654a4a90746f0a83a9ef)
set(lsp_commit_mb-clipper 7579c7025d35b390c5240b9bf06c63b3901e262e)
set(lsp_commit_oscilloscope b6a130069b35f81bc3bcebc32af7d687d9580e5d)
foreach(lsp clipper mb-clipper oscilloscope)
    set(commit ${lsp_commit_${lsp}})
    phinyx_stock_licence(lsp-plugins-${lsp} COPYING COPYING.LESSER)
    ExternalProject_Add(stock_lsp_${lsp}
        GIT_REPOSITORY https://github.com/lsp-plugins/lsp-plugins-${lsp}.git
        GIT_TAG ${commit}
        BUILD_IN_SOURCE TRUE
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND make config "FEATURES=clap ui" PREFIX=/usr COMMAND make fetch
        BUILD_COMMAND ${stock_make}
        INSTALL_COMMAND make install DESTDIR=<BINARY_DIR>/staging
            COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/staging/usr/lib/clap/lsp-plugins-${lsp}.clap ${PHINYX_STOCK_DIR}/
            ${stock_licence_commands})
endforeach()

add_custom_target(stock_plugins ALL DEPENDS
    stock_toneshifteq stock_wolfshaper stock_lsp_clipper stock_lsp_mb-clipper stock_lsp_oscilloscope)

install(DIRECTORY "${PHINYX_STOCK_DIR}/" DESTINATION "${PHINYX_STOCK_INSTALL_DIR}" USE_SOURCE_PERMISSIONS)
