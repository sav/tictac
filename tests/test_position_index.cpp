#include <catch2/catch_test_macros.hpp>
#include "storage/position_index.hpp"

#include <filesystem>

namespace fs = std::filesystem;

static fs::path temp_db() {
    auto p = fs::temp_directory_path() / "tictac_test_posindex";
    fs::remove_all(p);
    return p;
}

TEST_CASE("PositionIndex shard/suffix decomposition", "[posindex]") {
    using tictac::PositionIndex;

    tictac::ZobristKey key = 0xABC0'1234'5678'9DEFull;
    // Top 12 bits: 0xABC
    CHECK(PositionIndex::shard_of(key) == 0xABC);
    // Bottom 52 bits
    CHECK(PositionIndex::suffix_of(key) == (key & PositionIndex::SUFFIX_MASK));
}

TEST_CASE("PositionIndex insert and lookup via WAL", "[posindex]") {
    auto db = temp_db();
    tictac::PositionIndex idx(db);

    tictac::ZobristKey key1 = 0x123'0000'0000'0001;
    tictac::ZobristKey key2 = 0x456'0000'0000'0002;

    idx.insert(key1, 0, 5);
    idx.insert(key1, 1, 10);
    idx.insert(key2, 2, 3);

    // Before compact, lookup goes through WAL
    auto results1 = idx.lookup(key1);
    REQUIRE(results1.size() == 2);
    CHECK(results1[0].game_id == 0);
    CHECK(results1[0].ply == 5);
    CHECK(results1[1].game_id == 1);
    CHECK(results1[1].ply == 10);

    auto results2 = idx.lookup(key2);
    REQUIRE(results2.size() == 1);
    CHECK(results2[0].game_id == 2);

    CHECK(idx.total_entries() == 3);

    fs::remove_all(db);
}

TEST_CASE("PositionIndex compact and lookup from shards", "[posindex]") {
    auto db = temp_db();
    tictac::PositionIndex idx(db);

    tictac::ZobristKey key1 = 0x100'AAAA'BBBB'CCCC;
    tictac::ZobristKey key2 = 0x100'DDDD'EEEE'FFFF; // same shard (0x100)
    tictac::ZobristKey key3 = 0x200'1111'2222'3333; // different shard

    idx.insert(key1, 0, 1);
    idx.insert(key2, 1, 2);
    idx.insert(key3, 2, 3);
    idx.insert(key1, 3, 15); // another occurrence of key1

    idx.compact();

    // After compact, WAL should be empty, results from shard files
    auto results1 = idx.lookup(key1);
    REQUIRE(results1.size() == 2);

    auto results2 = idx.lookup(key2);
    REQUIRE(results2.size() == 1);
    CHECK(results2[0].game_id == 1);

    auto results3 = idx.lookup(key3);
    REQUIRE(results3.size() == 1);
    CHECK(results3[0].game_id == 2);

    CHECK(idx.total_entries() == 4);

    fs::remove_all(db);
}

TEST_CASE("PositionIndex incremental compact merges correctly", "[posindex]") {
    auto db = temp_db();
    tictac::PositionIndex idx(db);

    tictac::ZobristKey key = 0x555'ABCD'EF01'2345;

    // First batch
    idx.insert(key, 0, 1);
    idx.insert(key, 1, 2);
    idx.compact();

    // Second batch
    idx.insert(key, 2, 3);
    idx.insert(key, 3, 4);
    idx.compact();

    auto results = idx.lookup(key);
    REQUIRE(results.size() == 4);

    CHECK(idx.total_entries() == 4);

    fs::remove_all(db);
}

TEST_CASE("PositionIndex lookup with max_results", "[posindex]") {
    auto db = temp_db();
    tictac::PositionIndex idx(db);

    tictac::ZobristKey key = 0x777'0000'0000'0001;
    for (tictac::GameId g = 0; g < 50; ++g) {
        idx.insert(key, g, 10);
    }
    idx.compact();

    auto results = idx.lookup(key, 5);
    CHECK(results.size() == 5);

    auto all = idx.lookup(key);
    CHECK(all.size() == 50);

    fs::remove_all(db);
}

TEST_CASE("PositionIndex with real chess positions", "[posindex]") {
    auto db = temp_db();
    tictac::PositionIndex idx(db);

    // Generate real Zobrist hashes from chess positions
    chess::Board board;
    auto start_hash = board.hash();

    board.makeMove(chess::uci::uciToMove(board, "e2e4"));
    auto after_e4 = board.hash();

    board.makeMove(chess::uci::uciToMove(board, "e7e5"));
    auto after_e5 = board.hash();

    idx.insert(start_hash, 0, 0);
    idx.insert(after_e4, 0, 1);
    idx.insert(after_e5, 0, 2);
    idx.insert(start_hash, 1, 0); // another game also starts here

    idx.compact();

    auto start_results = idx.lookup(start_hash);
    REQUIRE(start_results.size() == 2);

    auto e4_results = idx.lookup(after_e4);
    REQUIRE(e4_results.size() == 1);
    CHECK(e4_results[0].game_id == 0);
    CHECK(e4_results[0].ply == 1);

    fs::remove_all(db);
}
