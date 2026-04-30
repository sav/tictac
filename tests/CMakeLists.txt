list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

add_executable(tictac_tests
    test_smoke.cpp
    test_mmap_file.cpp
    test_pgn_import.cpp
    test_game_store.cpp
    test_position_index.cpp
    test_sequence_index.cpp
    test_search.cpp
    test_cli.cpp
)

target_link_libraries(tictac_tests PRIVATE tictac_lib Catch2::Catch2WithMain)

catch_discover_tests(tictac_tests)
