#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uci {

struct EngineConfig {
    std::string path;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> options;
    int multipv = 1;
};

struct Limits {
    std::optional<int> depth;
    std::optional<int> movetime_ms;
    std::optional<int> nodes;
};

struct PvLine {
    int multipv = 1;
    std::optional<int> cp;
    std::optional<int> mate;
    int depth = 0;
    std::string moves;
};

struct Analysis {
    std::string fen;
    std::string bestmove;
    std::vector<PvLine> lines;
};

// Generic adapter for any UCI-compliant engine binary. Drives the engine over
// stdin/stdout on a dedicated worker thread; analyze() is non-blocking and
// returns a future fulfilled when the engine reports `bestmove`.
class UciEngine {
public:
    UciEngine(EngineConfig config, Limits limits);
    ~UciEngine();

    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    void newGame();
    std::future<Analysis> analyze(std::string fen);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace uci
