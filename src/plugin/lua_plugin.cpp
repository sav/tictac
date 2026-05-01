#include "plugin/lua_plugin.hpp"

#include <chrono>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include "chess.hpp"
#include "engine/engine_interface.hpp"
#ifdef TICTAC_HAVE_VIZ
#include "viz/viz_session.hpp"
#endif

namespace tictac {

namespace {

constexpr const char* kBoardMetatable = "tictac.Board";

const char* result_string(chess::GameResult r) {
    switch (r) {
        case chess::GameResult::WIN:  return "win";
        case chess::GameResult::LOSE: return "lose";
        case chess::GameResult::DRAW: return "draw";
        case chess::GameResult::NONE: return "*";
    }
    return "*";
}

int board_gc(lua_State* L) {
    auto* b = static_cast<chess::Board*>(lua_touserdata(L, 1));
    if (b) b->~Board();
    return 0;
}

// Iterator function. Upvalues:
//   1: light userdata pointing at the GameRecord (held by C++ side; lifetime tied to the call)
//   2: full userdata containing chess::Board (its __gc cleans up)
//   3: integer next 1-based half-move index
int moves_iter(lua_State* L) {
    auto* game  = static_cast<const GameRecord*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto* board = static_cast<chess::Board*>(lua_touserdata(L, lua_upvalueindex(2)));
    auto idx    = static_cast<std::size_t>(lua_tointeger(L, lua_upvalueindex(3)));

    if (!game || !board || idx == 0 || idx > game->moves.size()) {
        return 0;
    }

    chess::Move m{game->moves[idx - 1]};

    std::string san;
    try { san = chess::uci::moveToSan(*board, m); }
    catch (...) { san = "?"; }

    std::string uci;
    try { uci = chess::uci::moveToUci(m); }
    catch (...) { uci = "?"; }

    try { board->makeMove(m); }
    catch (...) { /* best effort; subsequent SAN may degrade */ }

    lua_pushinteger(L, static_cast<lua_Integer>(idx));
    lua_pushlstring(L, san.data(), san.size());
    lua_pushlstring(L, uci.data(), uci.size());

    lua_pushinteger(L, static_cast<lua_Integer>(idx + 1));
    lua_replace(L, lua_upvalueindex(3));

    return 3;
}

// game:fen([ply]) -> FEN string at the requested half-move index.
//
// ply = 0 returns the starting position, ply = N returns the position after
// the first N half-moves, ply = game.move_count returns the final position.
//
// When called with no argument, falls back to game.ply (set by position
// search) or 0 (the starting position) if no match ply is available.
int game_fen(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "__game_ptr");
    if (!lua_islightuserdata(L, -1)) {
        return luaL_error(L, "game:fen(): missing __game_ptr");
    }
    auto* game = static_cast<const GameRecord*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    auto move_count = static_cast<lua_Integer>(game->moves.size());

    lua_Integer target_ply;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        target_ply = luaL_checkinteger(L, 2);
    } else {
        lua_getfield(L, 1, "ply");
        target_ply = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
        lua_pop(L, 1);
    }

    if (target_ply < 0 || target_ply > move_count) {
        return luaL_error(L,
            "game:fen(): ply %d out of range [0, %d]",
            static_cast<int>(target_ply),
            static_cast<int>(move_count));
    }

    chess::Board board;
    try {
        for (lua_Integer i = 0; i < target_ply; ++i) {
            chess::Move m{game->moves[static_cast<std::size_t>(i)]};
            board.makeMove(m);
        }
    } catch (const std::exception& e) {
        return luaL_error(L, "game:fen(): %s", e.what());
    }

    std::string fen = board.getFen();
    lua_pushlstring(L, fen.data(), fen.size());
    return 1;
}

// game:moves() -> stateful iterator yielding (idx, san, uci).
int game_moves(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "__game_ptr");
    if (!lua_islightuserdata(L, -1)) {
        return luaL_error(L, "game:moves(): missing __game_ptr");
    }
    void* gptr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    void* mem = lua_newuserdatauv(L, sizeof(chess::Board), 0);
    new (mem) chess::Board();

    if (luaL_newmetatable(L, kBoardMetatable)) {
        lua_pushcfunction(L, board_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);

    // Stack: [board_udata]. Build closure with three upvalues.
    lua_pushlightuserdata(L, gptr);   // upvalue 1
    lua_pushvalue(L, -2);             // upvalue 2: copy of board_udata (keeps it alive)
    lua_pushinteger(L, 1);            // upvalue 3
    lua_pushcclosure(L, moves_iter, 3);

    // Stack: [board_udata, closure]. Drop the dangling board_udata copy beneath the closure.
    lua_remove(L, -2);
    return 1;
}

void push_game_table(lua_State* L, const GameRecord& game,
                     std::optional<HalfMoveIdx> ply) {
    lua_createtable(L, 0, 12);

    lua_pushinteger(L, static_cast<lua_Integer>(game.id));
    lua_setfield(L, -2, "id");

    lua_pushlstring(L, game.header.white.data(), game.header.white.size());
    lua_setfield(L, -2, "white");

    lua_pushlstring(L, game.header.black.data(), game.header.black.size());
    lua_setfield(L, -2, "black");

    lua_pushlstring(L, game.header.event.data(), game.header.event.size());
    lua_setfield(L, -2, "event");

    lua_pushlstring(L, game.header.date.data(), game.header.date.size());
    lua_setfield(L, -2, "date");

    lua_pushinteger(L, game.header.white_elo);
    lua_setfield(L, -2, "white_elo");

    lua_pushinteger(L, game.header.black_elo);
    lua_setfield(L, -2, "black_elo");

    lua_pushstring(L, result_string(game.header.result));
    lua_setfield(L, -2, "result");

    lua_pushinteger(L, static_cast<lua_Integer>(game.moves.size()));
    lua_setfield(L, -2, "move_count");

    if (ply) {
        lua_pushinteger(L, static_cast<lua_Integer>(*ply));
        lua_setfield(L, -2, "ply");
    }

    lua_pushlightuserdata(L, const_cast<GameRecord*>(&game));
    lua_setfield(L, -2, "__game_ptr");

    lua_pushcfunction(L, game_moves);
    lua_setfield(L, -2, "moves");

    lua_pushcfunction(L, game_fen);
    lua_setfield(L, -2, "fen");
}

int l_engine_analyze(lua_State* L) {
    auto* engine = static_cast<EngineInterface*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!engine || !engine->is_running()) {
        return luaL_error(L, "tictac.engine: not running");
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    std::string fen;
    bool have_fen = false;
    lua_getfield(L, 1, "fen");
    if (lua_isstring(L, -1)) {
        fen = lua_tostring(L, -1);
        have_fen = true;
    }
    lua_pop(L, 1);

    std::vector<std::string> moves;
    lua_getfield(L, 1, "moves");
    if (lua_istable(L, -1)) {
        lua_Integer n = luaL_len(L, -1);
        moves.reserve(static_cast<std::size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_geti(L, -1, i);
            if (lua_isstring(L, -1)) moves.emplace_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    if (!have_fen && moves.empty()) {
        return luaL_error(L,
            "tictac.engine.analyze: requires opts.fen or opts.moves");
    }

    lua_getfield(L, 1, "depth");
    auto depth = static_cast<unsigned>(luaL_optinteger(L, -1, 0));
    lua_pop(L, 1);
    lua_getfield(L, 1, "movetime");
    auto movetime_ms = static_cast<long long>(luaL_optinteger(L, -1, 0));
    lua_pop(L, 1);
    lua_getfield(L, 1, "multipv");
    auto multipv = static_cast<unsigned>(luaL_optinteger(L, -1, 1));
    lua_pop(L, 1);

    AnalysisResult r;
    try {
        engine->set_position(fen, moves);
        r = engine->analyze(std::chrono::milliseconds(movetime_ms), depth, multipv);
    } catch (const std::exception& e) {
        return luaL_error(L, "tictac.engine.analyze: %s", e.what());
    }

    auto push_pv = [&](const PvLine& pv) {
        lua_createtable(L, 0, 6);
        if (pv.is_mate) {
            lua_pushinteger(L, pv.mate_in);
            lua_setfield(L, -2, "mate");
        } else {
            lua_pushinteger(L, pv.score_cp);
            lua_setfield(L, -2, "score");
        }
        lua_pushinteger(L, pv.depth);    lua_setfield(L, -2, "depth");
        lua_pushinteger(L, pv.seldepth); lua_setfield(L, -2, "seldepth");
        lua_pushinteger(L, static_cast<lua_Integer>(pv.nodes));
        lua_setfield(L, -2, "nodes");

        lua_createtable(L, static_cast<int>(pv.pv.size()), 0);
        for (std::size_t j = 0; j < pv.pv.size(); ++j) {
            lua_pushlstring(L, pv.pv[j].data(), pv.pv[j].size());
            lua_seti(L, -2, static_cast<lua_Integer>(j + 1));
        }
        lua_setfield(L, -2, "pv");
    };

    lua_createtable(L, 0, 6);

    lua_createtable(L, static_cast<int>(r.lines.size()), 0);
    for (std::size_t i = 0; i < r.lines.size(); ++i) {
        push_pv(r.lines[i]);
        lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "lines");

    if (!r.lines.empty()) {
        const auto& pv0 = r.lines.front();
        if (pv0.is_mate) {
            lua_pushinteger(L, pv0.mate_in);
            lua_setfield(L, -2, "mate");
        } else {
            lua_pushinteger(L, pv0.score_cp);
            lua_setfield(L, -2, "score");
        }
        lua_pushinteger(L, pv0.depth); lua_setfield(L, -2, "depth");
        lua_createtable(L, static_cast<int>(pv0.pv.size()), 0);
        for (std::size_t j = 0; j < pv0.pv.size(); ++j) {
            lua_pushlstring(L, pv0.pv[j].data(), pv0.pv[j].size());
            lua_seti(L, -2, static_cast<lua_Integer>(j + 1));
        }
        lua_setfield(L, -2, "pv");
    }

    lua_pushinteger(L, static_cast<lua_Integer>(r.time.count()));
    lua_setfield(L, -2, "time_ms");
    lua_pushinteger(L, static_cast<lua_Integer>(r.total_nodes));
    lua_setfield(L, -2, "nodes");

    return 1;
}

int l_engine_set_option(lua_State* L) {
    auto* engine = static_cast<EngineInterface*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!engine || !engine->is_running()) {
        return luaL_error(L, "tictac.engine: not running");
    }
    const char* name  = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    try {
        engine->set_option(name, value);
    } catch (const std::exception& e) {
        return luaL_error(L, "%s", e.what());
    }
    return 0;
}

#ifdef TICTAC_HAVE_VIZ
int l_viz_add(lua_State* L) {
    auto* viz = static_cast<VizSession*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!viz) return luaL_error(L, "tictac.viz: not initialized");

    const char* fen = luaL_checkstring(L, 1);

    std::map<std::string, std::string> info;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            // key at -2, value at -1. Stringify both via lua_tolstring's
            // implicit number-to-string conversion (won't disturb the
            // iterator's original key, since we only stringify a copy).
            lua_pushvalue(L, -2);
            std::size_t klen = 0;
            const char* k = lua_tolstring(L, -1, &klen);
            std::size_t vlen = 0;
            const char* v = lua_tolstring(L, -2, &vlen);
            if (k && v) info.emplace(std::string(k, klen), std::string(v, vlen));
            lua_pop(L, 2);
        }
    }

    try {
        viz->add(fen, std::move(info));
    } catch (const std::exception& e) {
        return luaL_error(L, "tictac.viz.add: %s", e.what());
    }
    return 0;
}

int l_viz_count(lua_State* L) {
    auto* viz = static_cast<VizSession*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushinteger(L, viz ? static_cast<lua_Integer>(viz->size()) : 0);
    return 1;
}

void register_viz(lua_State* L, VizSession* viz) {
    lua_getglobal(L, "tictac");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "tictac");
    }

    lua_newtable(L); // tictac.viz

    lua_pushlightuserdata(L, viz);
    lua_pushcclosure(L, l_viz_add, 1);
    lua_setfield(L, -2, "add");

    lua_pushlightuserdata(L, viz);
    lua_pushcclosure(L, l_viz_count, 1);
    lua_setfield(L, -2, "count");

    lua_setfield(L, -2, "viz");
    lua_pop(L, 1);
}
#endif

void register_engine(lua_State* L, EngineInterface* engine) {
    lua_getglobal(L, "tictac");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "tictac");
    }

    lua_newtable(L); // tictac.engine

    lua_pushlightuserdata(L, engine);
    lua_pushcclosure(L, l_engine_analyze, 1);
    lua_setfield(L, -2, "analyze");

    lua_pushlightuserdata(L, engine);
    lua_pushcclosure(L, l_engine_set_option, 1);
    lua_setfield(L, -2, "set_option");

    lua_setfield(L, -2, "engine");
    lua_pop(L, 1); // pop tictac
}

} // namespace

LuaPlugin::LuaPlugin(const std::filesystem::path& script,
                     EngineInterface* engine,
                     VizSession* viz) {
    L_ = luaL_newstate();
    if (!L_) throw std::runtime_error("Failed to allocate Lua state");
    luaL_openlibs(L_);

    if (engine) register_engine(L_, engine);
#ifdef TICTAC_HAVE_VIZ
    if (viz) register_viz(L_, viz);
#else
    (void)viz;
#endif

    if (luaL_dofile(L_, script.string().c_str()) != LUA_OK) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_close(L_);
        L_ = nullptr;
        throw std::runtime_error("Lua plugin: " + err);
    }

    lua_getglobal(L_, "on_match");
    bool ok = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    if (!ok) {
        lua_close(L_);
        L_ = nullptr;
        throw std::runtime_error("Lua plugin: script must define on_match(game)");
    }
}

LuaPlugin::~LuaPlugin() {
    if (L_) lua_close(L_);
}

bool LuaPlugin::on_match(const GameRecord& game, std::optional<HalfMoveIdx> ply) {
    lua_getglobal(L_, "on_match");
    push_game_table(L_, game, ply);

    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        throw std::runtime_error("Lua plugin: " + err);
    }

    bool keep = lua_toboolean(L_, -1);
    lua_pop(L_, 1);
    return keep;
}

} // namespace tictac
