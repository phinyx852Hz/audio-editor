# SPDX-License-Identifier: AGPL-3.0-only
#
# libmysofa (SOFA HRTF files for the binaural renderer): the system's
# (Arch: libmysofa), or the pinned release built from its sources. libmysofa is
# by Symonics GmbH, Christian Hoene, under the BSD 3-Clause licence. The pinned
# source also brings the MIT KEMAR HRTF set, which is copied next to the app.

if(PkgConfig_FOUND)
    pkg_check_modules(MYSOFA QUIET IMPORTED_TARGET libmysofa>=1.2)
endif()

include(FetchContent)
FetchContent_Declare(mysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG v1.3.5
    GIT_SHALLOW TRUE)
# Its own build installs and tests too, so this only downloads it.
FetchContent_GetProperties(mysofa)
if(NOT mysofa_POPULATED)
    FetchContent_Populate(mysofa)
endif()

if(MYSOFA_FOUND)
    add_library(phi_mysofa INTERFACE)
    target_link_libraries(phi_mysofa INTERFACE PkgConfig::MYSOFA)
    message(STATUS "libmysofa: system ${MYSOFA_VERSION}")
else()
    find_package(ZLIB REQUIRED)
    set(_mysofa ${mysofa_SOURCE_DIR}/src)
    add_library(phi_mysofa STATIC
        ${_mysofa}/hrtf/reader.c ${_mysofa}/hdf/superblock.c ${_mysofa}/hdf/dataobject.c ${_mysofa}/hdf/btree.c
        ${_mysofa}/hdf/fractalhead.c ${_mysofa}/hdf/gunzip.c ${_mysofa}/hdf/gcol.c ${_mysofa}/hrtf/check.c
        ${_mysofa}/hrtf/spherical.c ${_mysofa}/hrtf/lookup.c ${_mysofa}/hrtf/tools.c ${_mysofa}/hrtf/kdtree.c
        ${_mysofa}/hrtf/neighbors.c ${_mysofa}/hrtf/interpolate.c ${_mysofa}/hrtf/resample.c ${_mysofa}/hrtf/loudness.c
        ${_mysofa}/hrtf/minphase.c ${_mysofa}/hrtf/easy.c ${_mysofa}/hrtf/cache.c ${_mysofa}/resampler/speex_resampler.c)
    set(_gen ${CMAKE_CURRENT_BINARY_DIR}/mysofa-gen)
    file(WRITE ${_gen}/config.h "#pragma once\n#define CMAKE_INSTALL_PREFIX \"/usr\"\n#define CPACK_PACKAGE_VERSION_MAJOR 1\n#define CPACK_PACKAGE_VERSION_MINOR 3\n#define CPACK_PACKAGE_VERSION_PATCH 5\n")
    file(WRITE ${_gen}/mysofa_export.h "#pragma once\n#define MYSOFA_EXPORT\n")
    target_include_directories(phi_mysofa PRIVATE ${_gen})
    target_include_directories(phi_mysofa SYSTEM PUBLIC ${_mysofa}/hrtf ${_gen})
    set_target_properties(phi_mysofa PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
    target_compile_options(phi_mysofa PRIVATE -w)
    target_link_libraries(phi_mysofa PUBLIC ZLIB::ZLIB m)
    message(STATUS "libmysofa: built from the pinned source")
endif()

# The default HRTF set, next to the executable while building and in share/phi/hrtf when installed.
set(PHI_DEFAULT_HRTF ${mysofa_SOURCE_DIR}/share/MIT_KEMAR_normal_pinna.sofa)
