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
    # Pure C++ FEN parser — always built so its tests can run even when Qt
    # is unavailable.
    viz/board_model.cpp
)

set(TICTAC_HAVE_VIZ OFF)
if(TICTAC_BUILD_VIZ)
    find_package(Qt6 QUIET COMPONENTS Widgets Svg)
    if(NOT Qt6_FOUND)
        find_package(Qt5 QUIET COMPONENTS Widgets Svg)
    endif()
    if(Qt6_FOUND OR Qt5_FOUND)
        set(TICTAC_HAVE_VIZ ON)
        list(APPEND TICTAC_LIB_SOURCES
            viz/board_widget.cpp
            viz/board_window.cpp
            viz/viz_session.cpp
            viz/pieces.qrc
        )
    else()
        message(WARNING
            "TICTAC_BUILD_VIZ=ON but Qt Widgets+Svg dev files were not found; "
            "the --viz subcommand will be disabled in this build.\n"
            "  Debian/Ubuntu:  sudo apt install libqt5svg5-dev   (or qt6-base-dev qt6-svg-dev)\n"
            "  Fedora:         sudo dnf install qt5-qtsvg-devel  (or qt6-qtsvg-devel)\n"
            "  Then re-run ./build.sh to pick the dependency up.")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
add_library(tictac_lib STATIC ${TICTAC_LIB_SOURCES})

target_include_directories(tictac_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tictac_lib
    PUBLIC chess_lib CLI11::CLI11 tictac_warnings
    PRIVATE Threads::Threads lua_lib SQLite::SQLite3
)

if(TICTAC_HAVE_VIZ)
    target_compile_definitions(tictac_lib PUBLIC TICTAC_HAVE_VIZ)
    if(Qt6_FOUND)
        target_link_libraries(tictac_lib PUBLIC Qt6::Widgets Qt6::Svg)
    else()
        target_link_libraries(tictac_lib PUBLIC Qt5::Widgets Qt5::Svg)
    endif()
endif()

add_executable(tictac main.cpp)
target_link_libraries(tictac PRIVATE tictac_lib)
