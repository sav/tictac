#include <catch2/catch_test_macros.hpp>
#include "chess.hpp"

TEST_CASE("chess-library smoke test", "[smoke]") {
    chess::Board board;
    REQUIRE(board.getFen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST_CASE("chess-library move generation", "[smoke]") {
    chess::Board board;
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    REQUIRE(moves.size() == 20);
}

TEST_CASE("chess-library Zobrist hashing", "[smoke]") {
    chess::Board board;
    auto hash1 = board.hash();

    chess::Board board2;
    auto hash2 = board2.hash();

    REQUIRE(hash1 == hash2);

    // Different position should have different hash
    board2.makeMove(chess::uci::uciToMove(board2, "e2e4"));
    REQUIRE(board.hash() != board2.hash());
}
