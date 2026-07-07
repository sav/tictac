// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// UCI chess engine driver: analysis types and the Engine interface.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tictac {

struct AnalysisLine {
    std::optional<double> score; // centipawns, side-to-move relative
    std::optional<int> mate;     // signed mate distance
    std::vector<std::string> pv; // UCI moves
};

struct Analysis {
    std::optional<double> score;
    std::optional<int> mate;
    int depth = 0;
    std::int64_t nodes = 0;
    std::int64_t time = 0; // milliseconds
    std::int64_t nps = 0;
    std::string bestmove;
    std::vector<std::string> pv;
    std::vector<AnalysisLine> lines; // populated when multipv > 1
};

struct AnalysisLimits {
    std::optional<int> depth;
    std::optional<int> movetime; // milliseconds
    std::optional<std::int64_t> nodes;
    int multipv = 1;
};

// A UCI engine subprocess. Spawned on construction, terminated on destruction.
class Engine {
public:
    Engine(std::string const &path, std::unordered_map<std::string, std::string> const &options);
    ~Engine();

    Engine(Engine const &) = delete;
    Engine &operator=(Engine const &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    void setOption(std::string const &name, std::string const &value);
    [[nodiscard]] Analysis analyse(std::string const &fen, AnalysisLimits const &limits);

private:
    void send(std::string const &line);
    std::string readLine();
    void waitFor(std::string const &token);

    int to_engine_ = -1;   // write end of engine's stdin
    int from_engine_ = -1; // read end of engine's stdout
    int pid_ = -1;
    std::string read_buffer_;
};

} // namespace tictac
