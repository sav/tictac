#include "viz/viz_session.hpp"

#include <QApplication>

#include "chess.hpp"
#include "viz/board_window.hpp"

namespace tictac {

VizSession::VizSession() {
    app_ = std::make_unique<QApplication>(argc_, argv_);
}

VizSession::~VizSession() = default;

void VizSession::add(const std::string& starting_fen,
                     const std::vector<std::string>& uci_moves,
                     std::map<std::string, std::string> info) {
    VizEntry e;
    e.info = std::move(info);

    chess::Board board;
    if (!starting_fen.empty()) {
        try { board.setFen(starting_fen); }
        catch (...) { board = chess::Board(); }
    }

    e.fens.push_back(board.getFen());
    for (const auto& uci : uci_moves) {
        chess::Move m;
        try {
            m = chess::uci::uciToMove(board, uci);
        } catch (...) { break; }
        try {
            board.makeMove(m);
        } catch (...) { break; }
        e.fens.push_back(board.getFen());
    }

    entries_.push_back(std::move(e));
}

int VizSession::run() {
    if (entries_.empty()) return 0;
    BoardWindow window(entries_);
    window.show();
    return app_->exec();
}

} // namespace tictac
