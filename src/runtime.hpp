// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: bound types and the Runtime interface.

#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <print>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

#include "engine.hpp"
#include "game.hpp"

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
    Writer(Writer &&) = delete;
    Writer &operator=(Writer &&) = delete;

    void write(std::string const &text) {
        std::print(*os_, "{}", text);
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

    void writeGame(std::shared_ptr<Game> const &game) {
        std::print(*os_, "{}\n", game->pgn());
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

private:
    std::ofstream file_;            // closed (and flushed) on destruction
    std::ostream *os_ = &std::cout; // borrows file_ when open, else std::cout
};

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
};

// A loaded plugin and its private execution context.
struct PluginInstance {
    sol::table plugin;
    sol::table ctx;
    std::shared_ptr<Args> args;
    std::string name;
    std::unordered_map<std::string, std::shared_ptr<Engine>> engines;
    std::vector<std::shared_ptr<Writer>> managed;
};

// One element of the pipeline value as it travels between plugins.
struct PluginValue {
    std::shared_ptr<Game> game;
    chess::Board board;
    sol::object data;
};

class Runtime {
public:
    explicit Runtime(RunOptions opts);
    int run();

private:
    void registerTypes();
    void loadPlugins();
    sol::table buildCtx(PluginInstance &inst);

    // Run one game through the whole pipeline; returns true if a plugin asked to stop
    // reading the database (via a "stop" or "abort" action).
    bool processGame(std::shared_ptr<Game> const &game, std::size_t index);

    RunOptions opts_;
    sol::state lua_;
    std::vector<std::unique_ptr<PluginInstance>> plugins_;
    sol::table shared_;
    std::shared_ptr<Writer> out_;
    std::size_t current_index_ = 0;
};

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
[[nodiscard]] PluginSpec parsePluginSpec(std::string const &spec);

} // namespace tictac
