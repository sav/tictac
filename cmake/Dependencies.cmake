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
