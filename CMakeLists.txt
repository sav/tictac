cmake_minimum_required(VERSION 3.20)
project(tictac VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TICTAC_BUILD_TESTS "Build tests" ON)
option(TICTAC_BUILD_BENCH "Build benchmarks" OFF)
option(TICTAC_ENABLE_SANITIZERS "Enable ASan+UBSan" OFF)
option(TICTAC_BUILD_VIZ "Build Qt-based FEN visualization" ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(TICTAC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TICTAC_BUILD_BENCH)
    add_subdirectory(bench)
endif()
