// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// UCI chess engine driver: analysis types and the Engine interface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

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
    int seldepth = 0; // deepest line the search actually reached
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

// One legal move's root policy prior, before search. lc0 exposes these through
// VerboseMoveStats; for a Maia network the prior is the modelled probability a
// human of that rating band plays the move.
struct MovePrior {
    std::string move;   // UCI
    double prior = 0.0; // [0, 1]
};

// A UCI engine subprocess. Spawned on construction, terminated on destruction.
class Engine final {
public:
    Engine(std::string const &path, std::unordered_map<std::string, std::string> const &options);
    ~Engine();

    Engine(Engine const &) = delete;
    Engine &operator=(Engine const &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    void setOption(std::string const &name, std::string const &value);
    [[nodiscard]] Analysis analyse(std::string const &fen, AnalysisLimits const &limits);

    // Root move policy priors (lc0 VerboseMoveStats). One entry per legal move
    // for an engine that emits "info string <move> ... (P: NN.NN%)" lines; an
    // empty vector for one that does not (e.g. Stockfish). A bare read searches
    // `nodes 1`, so the priors are the raw policy head rather than a search.
    [[nodiscard]] std::vector<MovePrior> policy(std::string const &fen, AnalysisLimits const &limits);

private:
    void send(std::string const &line);
    std::string readLine();
    void waitFor(std::string const &token);
    void syncReady();
    void handshake(std::unordered_map<std::string, std::string> const &options);
    void shutdown() noexcept;

    int to_engine_ = -1;   // write end of engine's stdin
    int from_engine_ = -1; // read end of engine's stdout
    ::pid_t pid_ = -1;
    std::string read_buffer_;
};

class EnginePool;

// RAII handle for an engine borrowed from an EnginePool: the engine returns to
// the pool's free list when the lease is dropped. Move-only; exactly one thread
// uses the borrowed engine for the lease's lifetime, which is what keeps the
// non-thread-safe Engine safe.
class EngineLease final {
public:
    EngineLease() = default;
    ~EngineLease();

    EngineLease(EngineLease const &) = delete;
    EngineLease &operator=(EngineLease const &) = delete;
    EngineLease(EngineLease &&other) noexcept;
    EngineLease &operator=(EngineLease &&other) noexcept;

    [[nodiscard]] Engine &operator*() const { return *engine_; }
    [[nodiscard]] Engine *operator->() const { return engine_; }
    [[nodiscard]] explicit operator bool() const { return engine_ != nullptr; }

private:
    friend class EnginePool;
    struct Slot;
    EngineLease(EnginePool *pool, Slot *slot, Engine *engine)
        : pool_(pool), slot_(slot), engine_(engine) {}

    EnginePool *pool_ = nullptr;
    Slot *slot_ = nullptr;
    Engine *engine_ = nullptr;
};

// A bounded, shared set of UCI engine subprocesses. Many worker threads borrow
// from it; each distinct (path, options) signature gets up to `maxPerKey`
// processes, created lazily and reused across borrows. Decoupling engines from
// workers is what lets the game pipeline run at -j N without spawning one engine
// per worker (which would oversubscribe the machine).
class EnginePool final {
public:
    // Defined out of line: the slots_ map holds unique_ptr<Slot>, and Slot is
    // only complete in the .cpp, so the member-cleanup these would emit must be
    // instantiated there too.
    explicit EnginePool(std::size_t maxPerKey);
    ~EnginePool();

    EnginePool(EnginePool const &) = delete;
    EnginePool &operator=(EnginePool const &) = delete;

    // Borrow an engine for (path, options), blocking while all `maxPerKey`
    // processes for that signature are in use. The returned lease releases the
    // engine on destruction.
    [[nodiscard]] EngineLease
    borrow(std::string const &path, std::unordered_map<std::string, std::string> const &options);

private:
    friend class EngineLease;
    using Slot = EngineLease::Slot;
    void release(Slot *slot, Engine *engine) noexcept;

    std::size_t const max_;
    std::mutex map_mu_;                                         // guards slots_
    std::unordered_map<std::string, std::unique_ptr<Slot>> slots_;
    std::mutex create_mu_; // serializes fork+exec across every key
};

} // namespace tictac
