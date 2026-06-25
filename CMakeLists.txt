cmake_minimum_required(VERSION 3.14)
project(tictac VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencies -----------------------------------------------------------
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

# reproc drives the external UCI engine over stdin/stdout. We only need the C++
# wrapper (reproc++); disable tests/examples to keep the build lean.
set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  reproc
  GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
  GIT_TAG v14.2.5
)
FetchContent_MakeAvailable(reproc)

# --- Targets ----------------------------------------------------------------
add_executable(tictac
  src/main.cpp
  src/uci_engine.cpp
)
target_link_libraries(tictac PRIVATE CLI11::CLI11 reproc++)
target_include_directories(tictac PRIVATE ${chesslib_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tictac PRIVATE -Wall -Wextra)
  target_compile_options(tictac PRIVATE $<$<CONFIG:Debug>:-g3>)
endif()
