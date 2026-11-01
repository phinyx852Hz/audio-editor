# SPDX-License-Identifier: AGPL-3.0-only
#
# Lua 5.4 for Tools > Scripts (Source/script, docs/scripting.md): the pinned
# release, built here so every PHI has the same Lua. Lua is by PUC-Rio, under
# the MIT licence. It is compiled as C++, so an error in a script unwinds
# PHI's own C++ frames properly instead of jumping over them.

include(FetchContent)
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

set(phi_lua_sources)
foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit liolib
             llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable
             ltablib ltm lundump lutf8lib lvm lzio)
    list(APPEND phi_lua_sources ${lua_SOURCE_DIR}/${name}.c)
endforeach()
set_source_files_properties(${phi_lua_sources} PROPERTIES LANGUAGE CXX)

add_library(phi_lua STATIC ${phi_lua_sources})
target_include_directories(phi_lua SYSTEM PUBLIC ${lua_SOURCE_DIR})
target_compile_definitions(phi_lua PUBLIC LUA_USE_LINUX)
set_target_properties(phi_lua PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE CXX)
target_compile_options(phi_lua PRIVATE -w -x c++)
target_link_libraries(phi_lua PUBLIC ${CMAKE_DL_LIBS} m)
message(STATUS "Lua: built from the pinned source (5.4.7)")
