# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# Build configuration for the tictac executable and its dependencies.

cmake_minimum_required(VERSION 3.14)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TICTAC_VERSION LIMIT_COUNT 1)
project(tictac VERSION ${TICTAC_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# CLI11 for command-line argument parsing. Pulled in at configure time so the
# build is self-contained and does not depend on a system-installed copy.
include(FetchContent)
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Disservin/chess-library provides the PGN parser (header-only chess.hpp).
# The repo ships no git tags/releases and no CMake target, so we pin a commit
# and add its include directory manually.
FetchContent_Declare(
  chesslib
  GIT_REPOSITORY https://github.com/Disservin/chess-library.git
  GIT_TAG 57159616358c2d4c09e0d4fb1562ca7f2d36a2fb
)
FetchContent_MakeAvailable(chesslib)

# Lua interpreter (CMake-enabled mirror of the upstream sources). Provides the
# `lua_static` target used to embed the plugin runtime. Its CMakeLists predates
# CMake 3.5, so relax the minimum policy version for the sub-build. The variable
# is a cache entry, so restoring it afterwards needs FORCE too; when it was unset
# to begin with the restore leaves an empty entry, which CMake treats as unset.
set(tt_saved_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/walterschell/Lua.git
  GIT_TAG v5.4.5
)
set(LUA_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(LUA_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(LUA_BUILD_BINARY OFF CACHE BOOL "" FORCE)
set(LUA_BUILD_COMPILER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lua)
set(CMAKE_POLICY_VERSION_MINIMUM "${tt_saved_policy_min}" CACHE STRING "" FORCE)

# sol2 is a header-only C++ <-> Lua binding.
FetchContent_Declare(
  sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444 # 3.5.0 (develop); v3.3.0 fails on Clang 19
)
FetchContent_MakeAvailable(sol2)

add_executable(tictac
  src/main.cpp
  src/app.cpp
  src/game.cpp
  src/engine.cpp
  src/options.cpp
  src/writer.cpp
  src/runtime.cpp
)
target_compile_definitions(tictac PRIVATE
  SOL_ALL_SAFETIES_ON=1
  # SOL_ALL_SAFETIES_ON forces sol2's internal stderr prints on; silence them
  # since the runtime already catches and reports every plugin error itself.
  SOL_PRINT_ERRORS=0
  TICTAC_VERSION="${TICTAC_VERSION}")
target_link_libraries(tictac PRIVATE CLI11::CLI11 lua_static Threads::Threads)
target_include_directories(tictac SYSTEM PRIVATE
  ${chesslib_SOURCE_DIR}/include
  ${sol2_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tictac PRIVATE -Wall -Wextra -Wpedantic -Wshadow
  -Wnon-virtual-dtor -Woverloaded-virtual -Wformat=2 -Wimplicit-fallthrough
  -Wold-style-cast -Wconversion -Wcast-align -Wcast-qual -Wnull-dereference
  -Wunused $<$<CONFIG:CI>:-Werror>)
  target_compile_options(tictac PRIVATE $<$<CONFIG:Debug>:-g3>)
endif()

# Test suite: drives the tictac binary through ctest (see tests/CMakeLists.txt).
enable_testing()
add_subdirectory(tests)
