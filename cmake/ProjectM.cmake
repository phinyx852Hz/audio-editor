# SPDX-License-Identifier: AGPL-3.0-only
#
# projectM (MilkDrop presets in the Visualizer): the system's when it can render
# into a given framebuffer (projectM 4.2 and later), otherwise a pinned commit
# built as static libraries. projectM is by the projectM team, LGPL-2.1.
# Sets PHI_HAVE_PROJECTM and the target phi_projectm.

option(PHI_PROJECTM "MilkDrop presets in the Visualizer, through projectM" ON)
set(PHI_HAVE_PROJECTM OFF)
if(NOT PHI_PROJECTM)
    return()
endif()

find_package(OpenGL)
find_package(Qt6 QUIET COMPONENTS OpenGL)
if(NOT OpenGL_FOUND OR NOT TARGET Qt6::OpenGL)
    message(STATUS "projectM: no OpenGL (or Qt6 OpenGL), building without MilkDrop presets")
    return()
endif()

add_library(phi_projectm INTERFACE)
target_link_libraries(phi_projectm INTERFACE Qt6::OpenGL)
target_compile_definitions(phi_projectm INTERFACE PHI_HAVE_PROJECTM=1)

find_package(projectM4 CONFIG QUIET)
find_package(projectM4Playlist CONFIG QUIET)
set(_system_ok OFF)
if(projectM4_FOUND AND projectM4Playlist_FOUND)
    include(CheckCXXSymbolExists)
    get_target_property(_pm_includes libprojectM::projectM INTERFACE_INCLUDE_DIRECTORIES)
    set(CMAKE_REQUIRED_INCLUDES ${_pm_includes})
    check_cxx_symbol_exists(projectm_opengl_render_frame_fbo "projectM-4/projectM.h" PHI_PROJECTM_HAS_FBO)
    unset(CMAKE_REQUIRED_INCLUDES)
    if(PHI_PROJECTM_HAS_FBO)
        set(_system_ok ON)
    endif()
endif()

if(_system_ok)
    target_link_libraries(phi_projectm INTERFACE libprojectM::projectM libprojectM::playlist)
    message(STATUS "projectM: system ${projectM4_VERSION}")
else()
    include(ExternalProject)
    set(_pm_prefix ${CMAKE_CURRENT_BINARY_DIR}/projectm)
    set(_pm_libs ${_pm_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}projectM-4-playlist${CMAKE_STATIC_LIBRARY_SUFFIX}
                 ${_pm_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}projectM-4${CMAKE_STATIC_LIBRARY_SUFFIX})
    ExternalProject_Add(phi_projectm_build
        GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
        GIT_TAG 349b7cc2db312d47cef6be24c6856ced6643a385
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_COMMAND ""
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${_pm_prefix} -DCMAKE_INSTALL_LIBDIR=lib
                   -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_SDL_UI=OFF
                   -DBUILD_TESTING=OFF -DENABLE_PLAYLIST=ON -DBUILD_DOCS=OFF
                   -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        BUILD_BYPRODUCTS ${_pm_libs})
    file(MAKE_DIRECTORY ${_pm_prefix}/include)
    add_dependencies(phi_projectm phi_projectm_build)
    target_include_directories(phi_projectm SYSTEM INTERFACE ${_pm_prefix}/include)
    target_compile_definitions(phi_projectm INTERFACE PROJECTM_STATIC_DEFINE PROJECTM_CXX_STATIC_DEFINE)
    target_link_libraries(phi_projectm INTERFACE ${_pm_libs} OpenGL::GL ${CMAKE_DL_LIBS})
    message(STATUS "projectM: built from the pinned source")
endif()
set(PHI_HAVE_PROJECTM ON)
