#include <catch2/catch_test_macros.hpp>
#include "import/import_visitor.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static const fs::path FIXTURES = fs::path(__FILE__).parent_path() / "fixtures";

TEST_CASE("ImportVisitor parses a single game", "[import]") {
    std::vector<tictac::GameRecord> games;
    tictac::ImportVisitor visitor([&](tictac::GameRecord&& g) {
        games.push_back(std::move(g));
    });

    std::ifstream file(FIXTURES / "simple_game.pgn");
    REQUIRE(file.is_open());

    chess::pgn::StreamParser parser(file);
    auto err = parser.readGames(visitor);
    REQUIRE(!err.hasError());

    REQUIRE(games.size() == 1);

    auto& g = games[0];
    CHECK(g.id == 0);
    CHECK(g.header.white == "Player1");
    CHECK(g.header.black == "Player2");
    CHECK(g.header.event == "Test Game");
    CHECK(g.header.date == "2024.01.01");
    CHECK(g.header.result == chess::GameResult::WIN);
    CHECK(g.header.white_elo == 2000);
    CHECK(g.header.black_elo == 1800);

    // 1.e4 e5 2.Nf3 Nc6 3.Bb5 a6 4.Ba4 Nf6 5.O-O Be7 = 10 half-moves
    CHECK(g.moves.size() == 10);
}

TEST_CASE("ImportVisitor parses multiple games", "[import]") {
    std::vector<tictac::GameRecord> games;
    tictac::ImportVisitor visitor([&](tictac::GameRecord&& g) {
        games.push_back(std::move(g));
    });

    std::ifstream file(FIXTURES / "multi_game.pgn");
    REQUIRE(file.is_open());

    chess::pgn::StreamParser parser(file);
    parser.readGames(visitor);

    REQUIRE(games.size() == 3);

    CHECK(games[0].header.white == "Alpha");
    CHECK(games[0].header.result == chess::GameResult::WIN);
    CHECK(games[0].moves.size() == 5); // 1.e4 e5 2.Nf3 Nc6 3.Bb5

    CHECK(games[1].header.white == "Gamma");
    CHECK(games[1].header.result == chess::GameResult::LOSE);
    CHECK(games[1].moves.size() == 6); // 1.d4 d5 2.c4 e6 3.Nc3 Nf6

    CHECK(games[2].header.white == "Epsilon");
    CHECK(games[2].header.result == chess::GameResult::DRAW);
    CHECK(games[2].moves.size() == 8); // 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6

    // IDs should be sequential
    CHECK(games[0].id == 0);
    CHECK(games[1].id == 1);
    CHECK(games[2].id == 2);
}

TEST_CASE("ImportVisitor moves are replayable", "[import]") {
    std::vector<tictac::GameRecord> games;
    tictac::ImportVisitor visitor([&](tictac::GameRecord&& g) {
        games.push_back(std::move(g));
    });

    std::ifstream file(FIXTURES / "simple_game.pgn");
    chess::pgn::StreamParser parser(file);
    parser.readGames(visitor);
    REQUIRE(games.size() == 1);

    // Replay the compact moves on a fresh board
    chess::Board board;
    for (auto cm : games[0].moves) {
        chess::Move m(cm);
        board.makeMove(m);
    }

    // After 1.e4 e5 2.Nf3 Nc6 3.Bb5 a6 4.Ba4 Nf6 5.O-O Be7
    // Verify board state by checking a known piece placement
    // White king should have castled to g1
    CHECK(board.at(chess::Square::underlying::SQ_G1) == chess::Piece::WHITEKING);
    // Black bishop on e7
    CHECK(board.at(chess::Square::underlying::SQ_E7) == chess::Piece::BLACKBISHOP);
}
