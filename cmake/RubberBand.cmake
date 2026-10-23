# SPDX-License-Identifier: AGPL-3.0-only
#
# Rubber Band Library (pitch correction, later time stretching): the system's
# (Arch: rubberband), or the pinned release built from its single-file
# compilation unit. Rubber Band is by Particular Programs Ltd, under
# GPL-2.0-or-later.

if(PkgConfig_FOUND)
    pkg_check_modules(RUBBERBAND QUIET IMPORTED_TARGET rubberband>=3.0)
endif()

if(RUBBERBAND_FOUND)
    add_library(phi_rubberband INTERFACE)
    target_link_libraries(phi_rubberband INTERFACE PkgConfig::RUBBERBAND)
    message(STATUS "Rubber Band: system ${RUBBERBAND_VERSION}")
else()
    include(FetchContent)
    FetchContent_Declare(rubberband
        GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
        GIT_TAG v4.0.0
        GIT_SHALLOW TRUE)
    # Its own build is Meson, so this only downloads it.
    FetchContent_GetProperties(rubberband)
    if(NOT rubberband_POPULATED)
        FetchContent_Populate(rubberband)
    endif()
    add_library(phi_rubberband STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
    target_include_directories(phi_rubberband SYSTEM PUBLIC ${rubberband_SOURCE_DIR})
    set_target_properties(phi_rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(phi_rubberband PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(phi_rubberband PUBLIC Threads::Threads)
    message(STATUS "Rubber Band: built from the pinned source")
endif()
