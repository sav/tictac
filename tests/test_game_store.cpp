#include <catch2/catch_test_macros.hpp>
#include "storage/game_store.hpp"

#include <filesystem>

namespace fs = std::filesystem;

static fs::path temp_db() {
    auto p = fs::temp_directory_path() / "tictac_test_gamestore";
    fs::remove_all(p);
    return p;
}

TEST_CASE("GameStore append and load roundtrip", "[gamestore]") {
    auto db = temp_db();
    tictac::GameStore store(db);

    tictac::GameRecord game;
    game.header.white     = "Magnus";
    game.header.black     = "Hikaru";
    game.header.event     = "World Championship";
    game.header.date      = "2024.12.01";
    game.header.result    = chess::GameResult::DRAW;
    game.header.white_elo = 2830;
    game.header.black_elo = 2780;
    game.moves            = {0x1234, 0x5678, 0x9ABC};

    auto id = store.append(game);
    REQUIRE(id == 0);
    REQUIRE(store.count() == 1);

    auto loaded = store.load(id);
    CHECK(loaded.id == 0);
    CHECK(loaded.header.white == "Magnus");
    CHECK(loaded.header.black == "Hikaru");
    CHECK(loaded.header.event == "World Championship");
    CHECK(loaded.header.date == "2024.12.01");
    CHECK(loaded.header.result == chess::GameResult::DRAW);
    CHECK(loaded.header.white_elo == 2830);
    CHECK(loaded.header.black_elo == 2780);
    REQUIRE(loaded.moves.size() == 3);
    CHECK(loaded.moves[0] == 0x1234);
    CHECK(loaded.moves[1] == 0x5678);
    CHECK(loaded.moves[2] == 0x9ABC);

    fs::remove_all(db);
}

TEST_CASE("GameStore multiple games", "[gamestore]") {
    auto db = temp_db();
    tictac::GameStore store(db);

    for (int i = 0; i < 100; ++i) {
        tictac::GameRecord game;
        game.header.white = "White" + std::to_string(i);
        game.header.black = "Black" + std::to_string(i);
        game.moves.resize(static_cast<std::size_t>(i + 1), static_cast<tictac::CompactMove>(i));
        store.append(game);
    }

    REQUIRE(store.count() == 100);

    // Verify random access
    auto g50 = store.load(50);
    CHECK(g50.header.white == "White50");
    CHECK(g50.moves.size() == 51);

    auto g99 = store.load(99);
    CHECK(g99.header.white == "White99");
    CHECK(g99.moves.size() == 100);

    fs::remove_all(db);
}

TEST_CASE("GameStore persistence across instances", "[gamestore]") {
    auto db = temp_db();

    // Write games
    {
        tictac::GameStore store(db);
        tictac::GameRecord g;
        g.header.white = "Persistent";
        g.header.black = "Test";
        g.moves = {0x1111, 0x2222};
        store.append(g);
    }

    // Reopen and verify
    {
        tictac::GameStore store(db);
        REQUIRE(store.count() == 1);
        auto loaded = store.load(0);
        CHECK(loaded.header.white == "Persistent");
        CHECK(loaded.moves.size() == 2);
    }

    fs::remove_all(db);
}
