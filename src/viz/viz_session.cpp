#include "viz/viz_session.hpp"

#include <QApplication>

#include "viz/board_window.hpp"

namespace tictac {

VizSession::VizSession() {
    app_ = std::make_unique<QApplication>(argc_, argv_);
}

VizSession::~VizSession() = default;

void VizSession::add(std::string fen, std::map<std::string, std::string> info) {
    entries_.push_back({std::move(fen), std::move(info)});
}

int VizSession::run() {
    if (entries_.empty()) return 0;
    BoardWindow window(entries_);
    window.show();
    return app_->exec();
}

} // namespace tictac
