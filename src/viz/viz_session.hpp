#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "viz/board_window.hpp"

class QApplication;

namespace tictac {

/// Owns a QApplication for the lifetime of a search and buffers entries
/// the Lua plugin appends via tictac.viz.add(). After the search finishes,
/// the CLI calls run() to spin up the board browser.
class VizSession {
public:
    VizSession();
    ~VizSession();

    VizSession(const VizSession&) = delete;
    VizSession& operator=(const VizSession&) = delete;

    /// Append an entry. `starting_fen` may be empty (defaults to the
    /// standard initial position). Each UCI move in `uci_moves` is replayed
    /// from that base; an unparseable / illegal move truncates the entry's
    /// ply list at that point (best effort, no exception).
    void add(const std::string& starting_fen,
             const std::vector<std::string>& uci_moves,
             std::map<std::string, std::string> info);

    std::size_t size() const { return entries_.size(); }

    /// Spin up the Qt event loop with the buffered entries. Returns
    /// QApplication::exec()'s exit code, or 0 if the buffer is empty.
    int run();

private:
    int argc_ = 1;
    char arg0_[8] = {'t','i','c','t','a','c',0,0};
    char* argv_[2] = {arg0_, nullptr};
    std::unique_ptr<QApplication> app_;
    std::vector<VizEntry> entries_;
};

} // namespace tictac
