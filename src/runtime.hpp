#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <ostream>
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
    Writer(); // wraps stdout
    Writer(const std::string &path, const std::string &mode);

    void write(const std::string &text);
    void writeGame(const std::shared_ptr<Game> &game);

    std::ostream &stream() { return *os_; }

private:
    std::ofstream file_; // closed (and flushed) on destruction
    std::ostream *os_ = nullptr;
};

// A plugin's parsed CLI arguments with typed accessors.
class Args {
public:
    std::map<std::string, std::string> values;
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

enum class OnError { Abort, Skip, Warn };

struct PluginSpec {
    std::string path;
    std::vector<std::pair<std::string, std::string>> args;
};

struct RunOptions {
    std::vector<std::string> files;
    std::vector<PluginSpec> plugins;
    std::string output = "-";
    bool noOutput = false;
    OnError onError = OnError::Warn;
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
struct PValue {
    std::shared_ptr<Game> game;
    chess::Board board;
    sol::object data;
};

class Runtime {
public:
    explicit Runtime(RunOptions opts);
    int run();

private:
    void registerTypes();          // bindings.cpp
    void loadPlugins();            // pipeline.cpp
    sol::table buildCtx(PluginInstance &inst); // bindings.cpp

    // Run one game through the whole pipeline; returns true if a plugin halted.
    bool processGame(const std::shared_ptr<Game> &game, std::size_t index);

    RunOptions opts_;
    sol::state lua_;
    std::vector<std::unique_ptr<PluginInstance>> plugins_;
    sol::table shared_;
    std::shared_ptr<Writer> out_;
    std::size_t current_index_ = 0;
};

// Build a result-producing analysis table for an Engine call.
sol::table analysisToTable(sol::state_view lua, const Analysis &a);

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
PluginSpec parsePluginSpec(const std::string &spec);

} // namespace tictac
