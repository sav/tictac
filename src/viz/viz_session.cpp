#include "viz/viz_session.hpp"

#include <QApplication>
#include <QDir>

#include "chess.hpp"
#include "viz/board_window.hpp"

// Static-library resources need an explicit init: Qt only auto-registers
// .qrc data when the resource is linked into the final executable, not
// when it's pulled in transitively via a static archive.
static void init_viz_resources() {
    Q_INIT_RESOURCE(pieces);
}

namespace tictac {

VizSession::VizSession() {
    init_viz_resources();
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
