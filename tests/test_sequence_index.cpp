#include <catch2/catch_test_macros.hpp>
#include "storage/sequence_index.hpp"

#include <filesystem>

namespace fs = std::filesystem;

static fs::path temp_db() {
    auto p = fs::temp_directory_path() / "tictac_test_seqindex";
    fs::remove_all(p);
    return p;
}

// Helper: parse SAN moves via a chess::Board to get CompactMoves
static std::vector<tictac::CompactMove> san_to_compact(const std::vector<std::string>& sans) {
    chess::Board board;
    std::vector<tictac::CompactMove> result;
    for (const auto& san : sans) {
        auto m = chess::uci::parseSan(board, san);
        result.push_back(m.move());
        board.makeMove(m);
    }
    return result;
}

TEST_CASE("SequenceIndex basic insert and search", "[seqindex]") {
    auto db = temp_db();
    tictac::SequenceIndex idx(db);

    auto ruy_lopez = san_to_compact({"e4", "e5", "Nf3", "Nc6", "Bb5"});
    auto italian   = san_to_compact({"e4", "e5", "Nf3", "Nc6", "Bc4"});
    auto sicilian  = san_to_compact({"e4", "c5", "Nf3", "d6"});

    idx.insert(0, ruy_lopez);
    idx.insert(1, italian);
    idx.insert(2, sicilian);

    // Root: all 3 games
    CHECK(idx.count({}) == 3);

    // After 1.e4: all 3 games
    auto e4_only = san_to_compact({"e4"});
    CHECK(idx.count(e4_only) == 3);

    // After 1.e4 e5: games 0 and 1
    auto e4_e5 = san_to_compact({"e4", "e5"});
    CHECK(idx.count(e4_e5) == 2);

    // After 1.e4 c5: game 2 only
    auto e4_c5 = san_to_compact({"e4", "c5"});
    CHECK(idx.count(e4_c5) == 1);

    // Ruy Lopez specifically: game 0 only
    CHECK(idx.count(ruy_lopez) == 1);

    // Non-existent opening
    auto queens_gambit = san_to_compact({"d4", "d5"});
    CHECK(idx.count(queens_gambit) == 0);

    fs::remove_all(db);
}

TEST_CASE("SequenceIndex search_prefix returns correct game IDs", "[seqindex]") {
    auto db = temp_db();
    tictac::SequenceIndex idx(db);

    auto opening = san_to_compact({"e4", "e5", "Nf3"});
    idx.insert(10, opening);
    idx.insert(20, opening);
    idx.insert(30, san_to_compact({"d4", "d5"}));

    auto e4_e5 = san_to_compact({"e4", "e5"});
    auto results = idx.search_prefix(e4_e5);
    REQUIRE(results.size() == 2);
    CHECK(results[0] == 10);
    CHECK(results[1] == 20);

    fs::remove_all(db);
}

TEST_CASE("SequenceIndex persistence save/load", "[seqindex]") {
    auto db = temp_db();

    auto moves = san_to_compact({"e4", "e5", "Nf3"});

    {
        tictac::SequenceIndex idx(db);
        idx.insert(0, moves);
        idx.insert(1, moves);
        idx.save();
    }

    {
        tictac::SequenceIndex idx(db);
        idx.load();
        CHECK(idx.count(moves) == 2);

        auto e4 = san_to_compact({"e4"});
        CHECK(idx.count(e4) == 2);
    }

    fs::remove_all(db);
}

TEST_CASE("SequenceIndex respects max_depth", "[seqindex]") {
    auto db = temp_db();
    tictac::SequenceIndex idx(db, 4); // Only index first 4 plies

    // A 6-ply sequence
    auto moves = san_to_compact({"e4", "e5", "Nf3", "Nc6", "Bb5", "a6"});
    idx.insert(0, moves);

    // First 4 plies should be indexed
    auto first4 = san_to_compact({"e4", "e5", "Nf3", "Nc6"});
    CHECK(idx.count(first4) == 1);

    // Searching beyond max_depth clips to max_depth (returns same as first4)
    auto first5 = san_to_compact({"e4", "e5", "Nf3", "Nc6", "Bb5"});
    CHECK(idx.count(first5) == 1); // clipped to 4 plies, same result

    fs::remove_all(db);
}
