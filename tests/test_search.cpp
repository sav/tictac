#include <catch2/catch_test_macros.hpp>

#include "import/import_visitor.hpp"
#include "search/search_engine.hpp"
#include "storage/game_store.hpp"
#include "storage/position_index.hpp"
#include "storage/sequence_index.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static const fs::path FIXTURES = fs::path(__FILE__).parent_path() / "fixtures";

static fs::path temp_db() {
    auto p = fs::temp_directory_path() / "tictac_test_search";
    fs::remove_all(p);
    return p;
}

// Import fixture PGN into a fresh database, indexing positions and sequences.
static void import_fixture(const fs::path& pgn, tictac::GameStore& store,
                           tictac::PositionIndex& pos_idx, tictac::SequenceIndex& seq_idx) {
    auto process_game = [&](tictac::GameRecord&& game) {
        auto id = store.append(game);

        chess::Board board;
        pos_idx.insert(board.hash(), id, 0);
        for (tictac::HalfMoveIdx ply = 0; ply < game.moves.size(); ++ply) {
            chess::Move m(game.moves[ply]);
            board.makeMove(m);
            pos_idx.insert(board.hash(), id, static_cast<tictac::HalfMoveIdx>(ply + 1));
        }

        seq_idx.insert(id, game.moves);
    };

    tictac::ImportVisitor visitor(process_game);
    std::ifstream file(pgn);
    chess::pgn::StreamParser parser(file);
    parser.readGames(visitor);
    pos_idx.compact();
}

TEST_CASE("SearchEngine position search with real games", "[search]") {
    auto db = temp_db();
    tictac::GameStore store(db);
    tictac::PositionIndex pos_idx(db);
    tictac::SequenceIndex seq_idx(db);

    import_fixture(FIXTURES / "multi_game.pgn", store, pos_idx, seq_idx);

    tictac::SearchEngine engine(store, pos_idx, seq_idx);

    // Starting position should appear in all 3 games
    auto start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto results = engine.search_position(start_fen);
    CHECK(results.size() == 3);

    // Position after 1.e4 should appear in games 0 (Ruy Lopez prefix) and 2 (Sicilian)
    auto after_e4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    results = engine.search_position(after_e4);
    CHECK(results.size() == 2);

    // Position after 1.d4 should appear only in game 1
    auto after_d4 = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1";
    results = engine.search_position(after_d4);
    REQUIRE(results.size() == 1);
    CHECK(results[0].header.white == "Gamma");

    fs::remove_all(db);
}

TEST_CASE("SearchEngine opening search", "[search]") {
    auto db = temp_db();
    tictac::GameStore store(db);
    tictac::PositionIndex pos_idx(db);
    tictac::SequenceIndex seq_idx(db);

    import_fixture(FIXTURES / "multi_game.pgn", store, pos_idx, seq_idx);

    tictac::SearchEngine engine(store, pos_idx, seq_idx);

    // 1.e4 appears in 2 games (games 0 and 2)
    auto results = engine.search_opening({"e4"});
    CHECK(results.size() == 2);

    // 1.e4 e5 appears in 1 game (game 0)
    results = engine.search_opening({"e4", "e5"});
    REQUIRE(results.size() == 1);
    CHECK(results[0].header.white == "Alpha");

    // 1.d4 appears in 1 game (game 1)
    results = engine.search_opening({"d4"});
    REQUIRE(results.size() == 1);
    CHECK(results[0].header.white == "Gamma");

    // Frequency counts
    CHECK(engine.opening_frequency({"e4"}) == 2);
    CHECK(engine.opening_frequency({"d4"}) == 1);
    CHECK(engine.opening_frequency({"c4"}) == 0);

    fs::remove_all(db);
}
