// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// One plugin chain over its own Lua state: bound types and the Pipeline
// interface.

#pragma once

#include "engine.hpp"
#include "game.hpp"
#include "options.hpp"
#include "writer.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

namespace tictac {

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

} // namespace tictac
