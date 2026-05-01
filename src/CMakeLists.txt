find_package(Threads REQUIRED)

set(TICTAC_LIB_SOURCES
    core/game_record.cpp
    storage/mmap_file.cpp
    import/import_visitor.cpp
    import/import_pipeline.cpp
    storage/game_store.cpp
    storage/db_manifest.cpp
    storage/position_index.cpp
    storage/sequence_index.cpp
    search/search_engine.cpp
    engine/uci_engine.cpp
    plugin/lua_plugin.cpp
    cli/cli_app.cpp
)

set(TICTAC_HAVE_VIZ OFF)
if(TICTAC_BUILD_VIZ)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(NOT Qt6_FOUND)
        find_package(Qt5 QUIET COMPONENTS Widgets)
    endif()
    if(Qt6_FOUND OR Qt5_FOUND)
        set(TICTAC_HAVE_VIZ ON)
        list(APPEND TICTAC_LIB_SOURCES
            viz/board_model.cpp
            viz/board_widget.cpp
            viz/board_window.cpp
            viz/viz_session.cpp
        )
    else()
        message(WARNING "TICTAC_BUILD_VIZ=ON but neither Qt6 nor Qt5 was found; building without visualization")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
add_library(tictac_lib STATIC ${TICTAC_LIB_SOURCES})

target_include_directories(tictac_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tictac_lib
    PUBLIC chess_lib CLI11::CLI11 tictac_warnings
    PRIVATE Threads::Threads lua_lib SQLite::SQLite3
)

if(TICTAC_HAVE_VIZ)
    target_compile_definitions(tictac_lib PUBLIC TICTAC_HAVE_VIZ)
    if(Qt6_FOUND)
        target_link_libraries(tictac_lib PUBLIC Qt6::Widgets)
    else()
        target_link_libraries(tictac_lib PUBLIC Qt5::Widgets)
    endif()
endif()

add_executable(tictac main.cpp)
target_link_libraries(tictac PRIVATE tictac_lib)
