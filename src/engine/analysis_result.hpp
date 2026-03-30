#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace tictac {

struct PvLine {
    int  score_cp   = 0;       // centipawns
    bool is_mate    = false;
    int  mate_in    = 0;       // moves to mate (if is_mate)
    unsigned depth    = 0;
    unsigned seldepth = 0;
    std::uint64_t nodes = 0;
    std::vector<std::string> pv; // principal variation in UCI notation
};

struct AnalysisResult {
    std::vector<PvLine> lines;
    std::uint64_t total_nodes = 0;
    std::chrono::milliseconds time{0};
};

} // namespace tictac
