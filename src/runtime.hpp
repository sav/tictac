// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime: output sinks, run options, bound types, the pipeline and
// the run that drives games through it.

#pragma once

#include "engine.hpp"
#include "game.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <print>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

namespace tictac {

// An output sink: a PGN/CSV/text file or the program's default output (stdout).
class Writer {
public:
    Writer() = default;

    explicit Writer(std::string const &path, bool append = false) {
        file_.open(path, append ? std::ios::app : std::ios::trunc);
        if (!file_) throw std::runtime_error("cannot open file: " + path);
        os_ = &file_;
    }

    Writer(Writer const &) = delete;
    Writer &operator=(Writer const &) = delete;
    // Not movable: os_ points at this object's own file_ member.
    Writer(Writer &&) = delete;
    Writer &operator=(Writer &&) = delete;

    void write(std::string const &text) {
        std::scoped_lock const lock(mu_);
        std::print(*os_, "{}", text);
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

    void writeGame(std::shared_ptr<Game> const &game) {
        // Serialized outside the lock: the game belongs to the calling worker,
        // and holding the sink while formatting would stall every other one.
        std::string const text = game->pgn();
        std::scoped_lock const lock(mu_);
        std::print(*os_, "{}\n", text);
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

private:
    // One record per lock, so records from parallel workers never interleave.
    std::mutex mu_;
    std::ofstream file_;            // closed (and flushed) on destruction
    std::ostream *os_ = &std::cout; // borrows file_ when open, else std::cout
};

// The ctx.open() writers, one per path for the whole run. Two plugins -- or two
// workers running the same plugin -- that open the same file get the same
// Writer, so their records interleave cleanly instead of two ofstreams fighting
// over one path and truncating each other.
class WriterRegistry {
public:
    // Throws when `path` was already opened with the other mode.
    [[nodiscard]] std::shared_ptr<Writer> open(std::string const &path, bool append);

    // Adopt an already-open writer, so a plugin opening the --output file gets
    // the stream the runtime is already writing games to.
    void adopt(std::string const &path, std::shared_ptr<Writer> writer);

private:
    struct Entry {
        std::shared_ptr<Writer> writer;
        bool append = false;
    };

    std::mutex mu_;
    std::unordered_map<std::string, Entry> writers_;
};

enum class OnError { Abort, Drop, Pass };

struct PluginSpec {
    std::string path;
    std::vector<std::pair<std::string, std::string>> args;
};

struct RunOptions {
    std::vector<std::string> files;
    std::vector<PluginSpec> plugins;
    std::string output = "-";
    bool noOutput = false;
    OnError onError = OnError::Abort;
    std::size_t jobs = 1; // pipelines run in parallel; always >= 1 by the time run() sees it
};

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
[[nodiscard]] PluginSpec parsePluginSpec(std::string const &spec);

// A plugin's parsed CLI arguments with typed accessors.
struct Args {
    std::vector<std::pair<std::string, std::string>> values;
};

// Lua-facing wrappers over the engine's board/move/game types.
struct LuaGame {
    std::shared_ptr<Game> g;
};

struct LuaBoard {
    chess::Board board;
};

struct LuaMove {
    chess::Move move;
    chess::Board before;        // position before the move (for SAN/piece/etc.)
    std::shared_ptr<Game> game; // keeps mainline moves alive; null for ad-hoc moves
    int ply = -1;               // index into game->moves; -1 if not a mainline move
};

// A loaded plugin and its private execution context.
struct PluginInstance {
    sol::table table;
    sol::table ctx;
    std::shared_ptr<Args> args;
    std::string name;
    std::unordered_map<std::string, std::shared_ptr<Engine>> engines; // one subprocess per path
};

// One element of the pipeline value as it travels between plugins.
struct PluginValue {
    std::shared_ptr<Game> game;
    chess::Board board;
    sol::object data;
};

// One independent execution of the whole plugin chain: its own Lua state, its
// own copy of every plugin, its own engines and ctx.scope. Only one thread
// touches a pipeline at a time.
class Pipeline {
public:
    // Loads every plugin in `opts.plugins` into a fresh Lua state, throwing when
    // one fails to load. `opts` and `writers` must outlive the pipeline.
    // `worker` is 1-based and `workers` is the total, as ctx reports them.
    Pipeline(
        RunOptions const &opts,
        std::shared_ptr<Writer> out,
        WriterRegistry &writers,
        std::size_t worker,
        std::size_t workers
    );

    Pipeline(Pipeline const &) = delete;
    Pipeline &operator=(Pipeline const &) = delete;

    // Run every init() hook in pipeline order; false when one failed, which is
    // always fatal to the run since the plugin cannot work without it.
    [[nodiscard]] bool init();

    // Run one game through the whole chain; true when a plugin asked to stop
    // reading the database (via a "stop" or "abort" action). Throws under
    // --on-error abort.
    [[nodiscard]] bool process(std::shared_ptr<Game> const &game, std::size_t index);

    // Run every finish() hook in pipeline order; a failure is only reported.
    void finish();

    // Drop the cached engine handles, quitting one subprocess per path.
    void closeEngines();

private:
    void registerTypes();
    void loadPlugins();

    sol::table buildCtx(PluginInstance &plugin);

    RunOptions const &opts_;
    std::shared_ptr<Writer> out_;
    WriterRegistry &writers_;
    std::size_t const worker_;  // 1-based; ctx.worker
    std::size_t const workers_; // ctx.workers
    sol::state lua_;
    std::vector<std::unique_ptr<PluginInstance>> plugins_;
    std::size_t index_ = 0; // current game; read by the ctx.log closures
};

// The run: reads the databases and drives the games through the pipelines.
class Runtime {
public:
    explicit Runtime(RunOptions opts);
    int run();

private:
    // opts_ backs the reference every pipeline holds, and the pipelines are
    // torn down before out_ and writers_ close their files, so the declaration
    // order here is load-bearing.
    RunOptions opts_;
    std::shared_ptr<Writer> out_; // the default game stream; null under --no-output
    WriterRegistry writers_;      // ctx.open() sinks, one per path for the whole run
    std::vector<std::unique_ptr<Pipeline>> pipelines_;
};

} // namespace tictac
