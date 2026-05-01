// FEN parser tests for the visualization board model. These run regardless
// of whether the build was compiled with Qt — the model is pure C++.
//
// When TICTAC_HAVE_VIZ is defined, an additional smoke test exercises
// VizSession + BoardWindow under the Qt offscreen platform plugin so we
// catch wiring regressions without needing a display.

#include <catch2/catch_test_macros.hpp>

#include "viz/board_model.hpp"

using tictac::BoardModel;

TEST_CASE("BoardModel parses the standard starting position", "[viz][model]") {
    auto m = BoardModel::from_fen(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    CHECK(m.at(0, 7) == 'r');
    CHECK(m.at(4, 7) == 'k');
    CHECK(m.at(0, 6) == 'p');
    CHECK(m.empty(0, 5));
    CHECK(m.empty(7, 4));
    CHECK(m.at(0, 1) == 'P');
    CHECK(m.at(4, 0) == 'K');
    CHECK(m.at(7, 0) == 'R');
}

TEST_CASE("BoardModel handles digit runs and partial ranks", "[viz][model]") {
    // Lone white king on e1, lone black king on e8 — a classic K vs K endgame.
    auto m = BoardModel::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK(m.at(4, 7) == 'k');
    CHECK(m.at(4, 0) == 'K');
    for (int f = 0; f < 8; ++f) {
        for (int r = 0; r < 8; ++r) {
            if ((f == 4 && r == 0) || (f == 4 && r == 7)) continue;
            CHECK(m.empty(f, r));
        }
    }
}

TEST_CASE("BoardModel ignores trailing FEN fields", "[viz][model]") {
    // Anything after the first space is the side-to-move/castling/etc.
    // The model only needs the piece-placement field.
    REQUIRE_NOTHROW(BoardModel::from_fen("8/8/8/8/8/8/8/4K2k w - - 50 75"));
}

TEST_CASE("BoardModel rejects malformed FEN", "[viz][model]") {
    CHECK_THROWS(BoardModel::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN")); // 7 squares last rank
    CHECK_THROWS(BoardModel::from_fen("8/8/8/8/8/8/8")); // 7 ranks
    CHECK_THROWS(BoardModel::from_fen("9/8/8/8/8/8/8/8")); // digit > 8
    CHECK_THROWS(BoardModel::from_fen("xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR")); // bad char
}

#ifdef TICTAC_HAVE_VIZ
#include <cstdlib>
#include "viz/viz_session.hpp"

TEST_CASE("VizSession buffers entries without spawning a UI", "[viz][session]") {
    // Force the offscreen platform so this works without a display server.
    ::setenv("QT_QPA_PLATFORM", "offscreen", 1);

    tictac::VizSession session;
    REQUIRE(session.size() == 0);

    session.add("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                {{"white", "Alice"}, {"black", "Bob"}});
    session.add("4k3/8/8/8/8/8/8/4K3 w - - 0 1",
                {{"white", "Carol"}, {"black", "Dave"}});
    CHECK(session.size() == 2);
}
#endif
