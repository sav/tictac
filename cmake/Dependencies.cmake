include(FetchContent)

# chess-library by Disservin (header-only, C++17, uses Meson -- no CMake target)
FetchContent_Declare(
    chess_library
    GIT_REPOSITORY https://github.com/Disservin/chess-library.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(chess_library)

add_library(chess_lib INTERFACE)
target_include_directories(chess_lib SYSTEM INTERFACE ${chess_library_SOURCE_DIR}/include)

# CLI11 for argument parsing
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CLI11)

# Catch2 for testing
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Lua 5.4 (no upstream CMakeLists; we list its C sources ourselves)
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)
FetchContent_MakeAvailable(lua)

set(LUA_SRC_DIR ${lua_SOURCE_DIR}/src)
set(LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lapi.c    ${LUA_SRC_DIR}/lcode.c    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldebug.c  ${LUA_SRC_DIR}/ldo.c      ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c   ${LUA_SRC_DIR}/lgc.c      ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmem.c    ${LUA_SRC_DIR}/lobject.c  ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/lparser.c ${LUA_SRC_DIR}/lstate.c   ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/ltable.c  ${LUA_SRC_DIR}/ltm.c      ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lvm.c     ${LUA_SRC_DIR}/lzio.c     ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c ${LUA_SRC_DIR}/lcorolib.c ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/liolib.c  ${LUA_SRC_DIR}/lmathlib.c ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/loslib.c  ${LUA_SRC_DIR}/lstrlib.c  ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/lutf8lib.c ${LUA_SRC_DIR}/linit.c
)
add_library(lua_lib STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua_lib SYSTEM PUBLIC ${LUA_SRC_DIR})
if(UNIX)
    target_compile_definitions(lua_lib PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua_lib PUBLIC ${CMAKE_DL_LIBS} m)
endif()
set_target_properties(lua_lib PROPERTIES C_STANDARD 99)
