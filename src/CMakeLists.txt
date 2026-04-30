find_package(Threads REQUIRED)

add_library(tictac_lib STATIC
    core/game_record.cpp
    storage/mmap_file.cpp
    import/import_visitor.cpp
    import/import_pipeline.cpp
    storage/game_store.cpp
    storage/db_manifest.cpp
    storage/position_index.cpp
    storage/sequence_index.cpp
    search/search_engine.cpp
    plugin/lua_plugin.cpp
    cli/cli_app.cpp
)

target_include_directories(tictac_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tictac_lib
    PUBLIC chess_lib CLI11::CLI11 tictac_warnings
    PRIVATE Threads::Threads lua_lib
)

add_executable(tictac main.cpp)
target_link_libraries(tictac PRIVATE tictac_lib)
