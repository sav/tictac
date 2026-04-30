#include "plugin/lua_plugin.hpp"

#include <new>
#include <stdexcept>
#include <string>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include "chess.hpp"

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
}

} // namespace

LuaPlugin::LuaPlugin(const std::filesystem::path& script) {
    L_ = luaL_newstate();
    if (!L_) throw std::runtime_error("Failed to allocate Lua state");
    luaL_openlibs(L_);

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
