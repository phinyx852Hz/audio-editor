# SPDX-License-Identifier: AGPL-3.0-only
#
# Vamp analysis plugin hosting (Source/engine/VampHost.cpp): the system's Vamp
# host SDK (Arch: vamp-plugin-sdk), or the pinned SDK built into a static
# library. The SDK is by Chris Cannam and Queen Mary, University of London,
# under a BSD-style licence; its KissFFT part is BSD-3-Clause.

if(PkgConfig_FOUND)
    pkg_check_modules(VAMP_HOSTSDK QUIET IMPORTED_TARGET vamp-hostsdk)
endif()

if(VAMP_HOSTSDK_FOUND)
    add_library(phinyx_vamp_host INTERFACE)
    target_link_libraries(phinyx_vamp_host INTERFACE PkgConfig::VAMP_HOSTSDK)
    message(STATUS "Vamp host SDK: system ${VAMP_HOSTSDK_VERSION}")
else()
    include(FetchContent)
    FetchContent_Declare(vampsdk
        GIT_REPOSITORY https://github.com/c4dm/vamp-plugin-sdk.git
        GIT_TAG c3ef877327f1b081b95a1d05349c297f8c4454f0)
    # The SDK has no CMakeLists.txt, so this only downloads it.
    FetchContent_MakeAvailable(vampsdk)
    set(vamp_src ${vampsdk_SOURCE_DIR}/src/vamp-hostsdk)
    add_library(phinyx_vamp_host STATIC
        ${vamp_src}/Files.cpp
        ${vamp_src}/PluginHostAdapter.cpp
        ${vamp_src}/RealTime.cpp
        ${vamp_src}/PluginBufferingAdapter.cpp
        ${vamp_src}/PluginChannelAdapter.cpp
        ${vamp_src}/PluginInputDomainAdapter.cpp
        ${vamp_src}/PluginLoader.cpp
        ${vamp_src}/PluginSummarisingAdapter.cpp
        ${vamp_src}/PluginWrapper.cpp
        ${vamp_src}/acsymbols.c)
    target_include_directories(phinyx_vamp_host SYSTEM PUBLIC ${vampsdk_SOURCE_DIR})
    set_target_properties(phinyx_vamp_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(phinyx_vamp_host PUBLIC ${CMAKE_DL_LIBS})
    message(STATUS "Vamp host SDK: built from the pinned source")
endif()
