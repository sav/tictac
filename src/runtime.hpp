// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: bound types and the Runtime interface.

#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

#include "engine.hpp"
#include "game.hpp"

namespace tictac {

// An output sink: a PGN/CSV/text file or the program's default output.
class Writer {
public:
    Writer() : os_(&std::cout) {}

    Writer(const std::string &path, const std::string &mode) {
        auto flags = std::ios::out;
        if (mode == "a") flags |= std::ios::app;
        else flags |= std::ios::trunc;
        file_.open(path, flags);
        if (!file_) throw std::runtime_error("cannot open writer: " + path);
        os_ = &file_;
    }

    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

    void write(const std::string &text) {
        *os_ << text << std::flush;
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

    void writeGame(const std::shared_ptr<Game> &game) {
        *os_ << game->pgn() << "\n" << std::flush;
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

    std::ostream &stream() { return *os_; }

private:
    std::ofstream file_; // closed (and flushed) on destruction
    std::ostream *os_ = nullptr;
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
    int jobs = 1;
};

// A loaded plugin and its private execution context.
struct PluginInstance {
    sol::table plugin;
    sol::table ctx;
    std::shared_ptr<Args> args;
    std::string name;
    std::map<std::string, std::shared_ptr<Engine>> engines;
    std::map<std::string, std::shared_ptr<Writer>> writers;
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

    // Run one game through the whole pipeline; returns true if a plugin halted.
    bool processGame(const std::shared_ptr<Game> &game, std::size_t index);

    RunOptions opts_;
    sol::state lua_;
    std::vector<std::unique_ptr<PluginInstance>> plugins_;
    sol::table shared_;
    std::shared_ptr<Writer> out_;
    std::size_t current_index_ = 0;
};

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
PluginSpec parsePluginSpec(const std::string &spec);

} // namespace tictac
