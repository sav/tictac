// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: sol2 bindings and pipeline execution.

#include "runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <ranges>
#include <sstream>
#include <stdexcept>

namespace tictac {

namespace {

std::optional<chess::Move> helper_parseMove(chess::Board const &board, std::string_view move) {
    // try to parse `move` as SAN first; fall back to matching an UCI format if it fails.
    try {
        return chess::uci::parseSan(board, move);
    } catch (chess::uci::SanParseError const &) {
    } catch (chess::uci::AmbiguousMoveError const &) {}
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (auto const &m : moves)
        if (chess::uci::moveToUci(m) == move) return m;
    return std::nullopt;
}

LuaMove helper_makeLuaMove(
    chess::Move mv, chess::Board const &before, std::shared_ptr<Game> game = nullptr, int ply = -1
) {
    return LuaMove{mv, before, std::move(game), ply};
}

std::string helper_squareStr(chess::Square sq) { return static_cast<std::string>(sq); }

sol::object helper_pieceAt(sol::this_state ts, chess::Board const &board, std::string const &sq) {
    if (!chess::Square::is_valid_string_sq(sq)) return sol::lua_nil;
    chess::Piece p = board.at(chess::Square(sq));
    if (p == chess::Piece::NONE) return sol::lua_nil;
    return sol::make_object(ts, static_cast<std::string>(p));
}

int helper_pieceValue(chess::PieceType pt) {
    switch (pt.internal()) {
    case chess::PieceType::PAWN: return 100;
    case chess::PieceType::KNIGHT: return 320;
    case chess::PieceType::BISHOP: return 330;
    case chess::PieceType::ROOK: return 500;
    case chess::PieceType::QUEEN: return 900;
    default: return 0;
    }
}

sol::table helper_analysisToTable(sol::state_view lua, Analysis const &a) {
    sol::table table = lua.create_table();
    if (a.score) table["score"] = *a.score;
    if (a.mate) table["mate"] = *a.mate;
    table["depth"] = a.depth;
    table["nodes"] = a.nodes;
    table["time"] = a.time;
    table["nps"] = a.nps;
    table["bestmove"] = a.bestmove;
    sol::table pv = lua.create_table();
    for (std::size_t i = 0; i < a.pv.size(); ++i) pv[i + 1] = a.pv[i];
    table["pv"] = pv;
    if (!a.lines.empty()) {
        sol::table lines = lua.create_table();
        for (std::size_t i = 0; i < a.lines.size(); ++i) {
            sol::table ln = lua.create_table();
            if (a.lines[i].score) ln["score"] = *a.lines[i].score;
            if (a.lines[i].mate) ln["mate"] = *a.lines[i].mate;
            sol::table lpv = lua.create_table();
            for (std::size_t j = 0; j < a.lines[i].pv.size(); ++j) lpv[j + 1] = a.lines[i].pv[j];
            ln["pv"] = lpv;
            lines[i + 1] = ln;
        }
        table["lines"] = lines;
    }
    return table;
}

AnalysisLimits helper_readLimits(sol::table const &table) {
    AnalysisLimits lim;
    if (table["depth"].valid()) lim.depth = table.get<int>("depth");
    if (table["movetime"].valid()) lim.movetime = table.get<int>("movetime");
    if (table["nodes"].valid()) lim.nodes = table.get<std::int64_t>("nodes");
    if (table["multipv"].valid()) lim.multipv = table.get<int>("multipv");
    return lim;
}

// All values stored for `key`, in the order they were passed (empty if none).
std::vector<std::string const *>
helper_findArgs(std::vector<std::pair<std::string, std::string>> const &values, std::string_view key) {
    std::vector<std::string const *> out;
    for (auto const &[k, v] : values)
        if (k == key) out.push_back(&v);
    return out;
}

// Like helper_findArgs, but drops entries with an explicitly empty value:
// `key=` is treated the same as `key` being absent. Every accessor except
// `bool` wants this -- `bool` treats an empty value as an explicit false
// instead.
std::vector<std::string const *> helper_findNonEmptyArgs(
    std::vector<std::pair<std::string, std::string>> const &values, std::string_view key
) {
    std::vector<std::string const *> out = helper_findArgs(values, key);
    std::erase_if(out, [](std::string const *v) { return v->empty(); });
    return out;
}

// Coerce the found values with `f`: a lone match yields the scalar, several
// yield a 1-based array in argument order. `found` must be non-empty.
template <typename F>
sol::object helper_scalarOrArray(sol::this_state ts, std::vector<std::string const *> const &found, F f) {
    if (found.size() == 1) return sol::make_object(ts, f(*found.front()));
    sol::table out = sol::state_view(ts).create_table();
    for (std::size_t i = 0; i < found.size(); ++i) out[i + 1] = f(*found[i]);
    return out;
}

// Like helper_scalarOrArray, but `f` may reject a value (returning nullopt). On
// the first rejection, print an error naming `key`, `typeName` (the expected
// type), and the offending value to stderr and return nil for the whole call.
// `found` must be non-empty.
template <typename F>
sol::object helper_scalarOrArrayChecked(
    sol::this_state ts,
    std::string_view key,
    std::string_view typeName,
    std::vector<std::string const *> const &found,
    F f
) {
    std::vector<sol::object> values;
    values.reserve(found.size());
    for (auto const *v : found) {
        auto coerced = f(*v);
        if (!coerced) {
            std::println(
                stderr, "error: invalid value '{}' for argument '{}': expected a {}", *v, key, typeName
            );
            return sol::lua_nil;
        }
        values.push_back(sol::make_object(ts, *coerced));
    }
    if (values.size() == 1) return values.front();
    sol::table out = sol::state_view(ts).create_table();
    for (std::size_t i = 0; i < values.size(); ++i) out[i + 1] = values[i];
    return out;
}

} // namespace

Runtime::Runtime(RunOptions opts) : opts_(std::move(opts)) {
    // clang-format off
    lua_.open_libraries(
		sol::lib::base,
		sol::lib::string,
		sol::lib::table,
		sol::lib::math,
		sol::lib::os,
		sol::lib::io,
		sol::lib::package
    );
    // clang-format on

    shared_ = lua_.create_table();

    if (!opts_.noOutput)
        out_ = opts_.output == "-" ? std::make_shared<Writer>() : std::make_shared<Writer>(opts_.output);

    registerTypes();
    loadPlugins();
}

int Runtime::run() {
    // Call init() for every plugin.
    for (auto &plugin : plugins_) {
        sol::optional<sol::protected_function> init = plugin->table["init"];
        if (init) {
            sol::protected_function_result r = (*init)(plugin->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::println(stderr, "error: init of '{}': {}", plugin->name, err.what());
                return 1; // init failure means the plugin can't run; always abort, regardless of --on-error
            }
        }
    }
    std::size_t index = 0;
    bool aborted = false;
    for (auto const &file : opts_.files) {
        if (aborted) break;
        std::ifstream in(file);
        if (!in) {
            std::println(stderr, "error: cannot open file: {}", file);
            return 1;
        }
        auto games = parseGames(in);
        for (auto &game : games) {
            ++index;
            current_index_ = index;
            if (processGame(game, index)) {
                aborted = true;
                break;
            }
        }
    }
    // Call finish() in pipeline order.
    for (auto &plugin : plugins_) {
        sol::optional<sol::protected_function> finish = plugin->table["finish"];
        if (finish) {
            sol::protected_function_result r = (*finish)(plugin->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::println(stderr, "error: finish of '{}': {}", plugin->name, err.what());
            }
        }
    }
    // Drop the runtime's cached engine handles; ~Engine quits and reaps each
    // subprocess once its last reference (the Lua-held copy included) is gone.
    for (auto &plugin : plugins_) {
        plugin->engines.clear();
    }

    return 0;
}

namespace {

// clang-format off
void registerTypes_Move(sol::state_view lua) {
    lua.new_usertype<LuaMove>(
        "Move", sol::no_constructor,
        "san", [](LuaMove &m) { return chess::uci::moveToSan(m.before, m.move); },
        "uci", [](LuaMove &m) { return chess::uci::moveToUci(m.move); },
        "from", [](LuaMove &m) { return helper_squareStr(m.move.from()); },
        "to", [](LuaMove &m) { return helper_squareStr(m.move.to()); },
        "piece", [](LuaMove &m) { return static_cast<std::string>(m.before.at<chess::PieceType>(m.move.from())); },
        "isCapture", [](LuaMove &m) { return m.before.isCapture(m.move); },
        "isCheck", [](LuaMove &m) {
            chess::Board b = m.before;
            b.makeMove(m.move);
            return b.inCheck();
        },
        "isPromotion", [](LuaMove &m) { return m.move.typeOf() == chess::Move::PROMOTION; },
        "promotion", [](LuaMove &m, sol::this_state ts) -> sol::object {
            if (m.move.typeOf() != chess::Move::PROMOTION) return sol::lua_nil;
            return sol::make_object(ts, static_cast<std::string>(m.move.promotionType()));
        },
        "comment", [](LuaMove &m, sol::this_state ts) -> sol::object {
            if (m.game && m.ply >= 0) {
                auto const &c = m.game->moves[static_cast<std::size_t>(m.ply)].comment;
                if (!c.empty()) return sol::make_object(ts, c);
            }
            return sol::lua_nil;
        },
        "setComment", [](LuaMove &m, std::string const &text) {
            if (m.game && m.ply >= 0) m.game->moves[static_cast<std::size_t>(m.ply)].comment = text;
        },
        "nags", [](LuaMove &m, sol::this_state ts) {
            sol::table table = sol::state_view(ts).create_table();
            if (m.game && m.ply >= 0) {
                auto const &n = m.game->moves[static_cast<std::size_t>(m.ply)].nags;
                for (std::size_t i = 0; i < n.size(); ++i) table[i + 1] = n[i];
            }
            return table;
        },
        "addNag", [](LuaMove &m, int code) {
            if (m.game && m.ply >= 0) m.game->moves[static_cast<std::size_t>(m.ply)].nags.push_back(code);
        }
    );
}

void registerTypes_Board(sol::state_view lua) {
    lua.new_usertype<LuaBoard>(
        "Board", sol::no_constructor,
        "fen", [](LuaBoard &b) { return b.board.getFen(); },
        "setFen", [](LuaBoard &b, std::string const &fen) { b.board.setFen(fen); },
        "sideToMove", [](LuaBoard &b) {
            return b.board.sideToMove() == chess::Color::WHITE ? std::string("white") : std::string("black");
        },
        "fullmoveNumber", [](LuaBoard &b) { return b.board.fullMoveNumber(); },
        "halfmoveClock", [](LuaBoard &b) { return b.board.halfMoveClock(); },
        "legalMoves", [](LuaBoard &b) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, b.board);
            std::vector<LuaMove> out;
            for (auto const &m : moves) out.push_back(helper_makeLuaMove(m, b.board));
            return sol::as_table(out);
        },
        "isLegal", [](LuaBoard &b, std::string const &mv) { return helper_parseMove(b.board, mv).has_value(); },
        "makeMove", [](LuaBoard &b, std::string const &mv) {
            auto parsed = helper_parseMove(b.board, mv);
            if (!parsed) throw std::runtime_error("illegal move: " + mv);
            LuaBoard nb{b.board};
            nb.board.makeMove(*parsed);
            return nb;
        },
        "piece", [](LuaBoard &b, std::string const &sq, sol::this_state ts) { return helper_pieceAt(ts, b.board, sq); },
        "pieces", [](LuaBoard &b, sol::optional<sol::table> filter, sol::this_state ts) {
            sol::table out = sol::state_view(ts).create_table();
            std::optional<chess::Color> fc;
            std::optional<chess::PieceType> ft;
            if (filter) {
                sol::table f = *filter;
                if (f["color"].valid()) {
                    fc = f.get<std::string>("color") == "white" ? chess::Color::WHITE : chess::Color::BLACK;
                }
                if (f["type"].valid()) ft = chess::PieceType(f.get<std::string>("type"));
            }
            for (int i = 0; i < 64; ++i) {
                chess::Square sq(i);
                chess::Piece p = b.board.at(sq);
                if (p == chess::Piece::NONE) continue;
                if (fc && p.color() != *fc) continue;
                if (ft && p.type() != *ft) continue;
                out[helper_squareStr(sq)] = static_cast<std::string>(p);
            }
            return out;
        },
        "isCheck", [](LuaBoard &b) { return b.board.inCheck(); },
        "isCheckmate", [](LuaBoard &b) {
            auto reason = b.board.isGameOver().first;
            return reason == chess::GameResultReason::CHECKMATE;
        },
        "isStalemate", [](LuaBoard &b) {
            auto reason = b.board.isGameOver().first;
            return reason == chess::GameResultReason::STALEMATE;
        },
        "isInsufficientMaterial", [](LuaBoard &b) { return b.board.isInsufficientMaterial(); },
        "isRepetition", [](LuaBoard &b, sol::optional<int> count) { return b.board.isRepetition(count.value_or(2)); },
        "phase",
        [](LuaBoard &b, sol::optional<int> openingMoves, sol::optional<int> endgameThreshold) {
            int total = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                if (p.type() == chess::PieceType::PAWN || p.type() == chess::PieceType::KING) continue;
                total += helper_pieceValue(p.type());
            }
            if (static_cast<int>(b.board.fullMoveNumber()) <= openingMoves.value_or(10)) return std::string("opening");
            if (total <= endgameThreshold.value_or(1300)) return std::string("endgame");
            return std::string("middlegame");
        },
        "material", [](LuaBoard &b, sol::this_state ts) {
            int white = 0, black = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                int v = helper_pieceValue(p.type());
                if (p.color() == chess::Color::WHITE) white += v;
                else black += v;
            }
            sol::table table = sol::state_view(ts).create_table();
            table["white"] = white;
            table["black"] = black;
            return table;
        }
    );
}

void registerTypes_Game(sol::state_view lua) {
    lua.new_usertype<LuaGame>(
        "Game", sol::no_constructor,
        "header", [](LuaGame &g, std::string const &key, sol::this_state ts) -> sol::object {
            std::string const *v = g.g->findHeader(key);
            if (!v) return sol::lua_nil;
            return sol::make_object(ts, *v);
        },
        "headers", [](LuaGame &g, sol::this_state ts) {
            sol::table table = sol::state_view(ts).create_table();
            for (auto const &[k, v] : g.g->headers) table[k] = v;
            return table;
        },
        "setHeader", [](LuaGame &g, std::string const &k, std::string const &v) { g.g->setHeader(k, v); },
        "removeHeader", [](LuaGame &g, std::string const &k) { return g.g->removeHeader(k); },
        "result", [](LuaGame &g) { return g.g->result(); },
        "moveCount", [](LuaGame &g) { return g.g->moveCount(); },
        "moves", [](LuaGame &g) {
            std::vector<LuaMove> out;
            chess::Board board = g.g->startBoard();
            for (std::size_t i = 0; i < g.g->moves.size(); ++i) {
                out.push_back(helper_makeLuaMove(g.g->moves[i].move, board, g.g, static_cast<int>(i)));
                board.makeMove(g.g->moves[i].move);
            }
            return sol::as_table(out);
        },
        "startBoard", [](LuaGame &g) { return LuaBoard{g.g->startBoard()}; },
        "board", [](LuaGame &g, sol::optional<int> ply) { return LuaBoard{g.g->boardAt(ply.value_or(-1))}; },
        "positions", [](LuaGame &g, sol::this_state ts) {
            sol::state_view l(ts);
            auto game = g.g;
            auto idx = std::make_shared<std::size_t>(0);
            return sol::make_object(
                l, std::function<sol::object(sol::this_state)>([game, idx](sol::this_state s) -> sol::object {
                    if (*idx >= game->moves.size()) return sol::lua_nil;
                    std::size_t i = *idx;
                    chess::Board before = game->boardAt(static_cast<int>(i));
                    chess::Board after = before;
                    after.makeMove(game->moves[i].move);
                    sol::table node = sol::state_view(s).create_table();
                    node["ply"] = i + 1;
                    node["move"] = helper_makeLuaMove(game->moves[i].move, before, game, static_cast<int>(i));
                    node["board_before"] = LuaBoard{before};
                    node["board_after"] = LuaBoard{after};
                    node["board"] = LuaBoard{after};
                    ++(*idx);
                    return node;
                }));
        },
        "pgn", [](LuaGame &g) { return g.g->pgn(); },
        "clone", [](LuaGame &g) { return LuaGame{g.g->clone()}; },
        sol::meta_function::to_string, [](LuaGame &g) {
            std::string const *white = g.g->findHeader("White");
            std::string const *black = g.g->findHeader("Black");
            return std::format("{} vs {}, {}", white ? *white : "?", black ? *black : "?", g.g->result());
        }
    );
}

void registerTypes_Writer(sol::state_view lua) {
    lua.new_usertype<Writer>(
        "Writer", sol::no_constructor,
        "write", &Writer::write,
        "writef", [](Writer &w, sol::this_state ts, std::string const &fmt, sol::variadic_args va) {
            sol::state_view l(ts);
            sol::protected_function format = l["string"]["format"];
            sol::protected_function_result r = format(fmt, va);
            if (!r.valid()) {
                sol::error e = r;
                throw std::runtime_error(e.what());
            }
            w.write(r.get<std::string>());
        },
        "writeGame", [](Writer &w, LuaGame &g) { w.writeGame(g.g); }
    );
}

void registerTypes_Engine(sol::state_view lua) {
    lua.new_usertype<Engine>(
        "Engine", sol::no_constructor,
        "setOption", [](Engine &e, std::string const &name, sol::object value) {
            e.setOption(name, value.as<std::string>());
        },
        "analyse", [](Engine &e, LuaBoard &b, sol::table limits, sol::this_state ts) {
            sol::state_view l(ts);
            return helper_analysisToTable(l, e.analyse(b.board.getFen(), helper_readLimits(limits)));
        },
        "bestmove", [](Engine &e, LuaBoard &b, sol::table limits) {
            return e.analyse(b.board.getFen(), helper_readLimits(limits)).bestmove;
        },
        "cp", [](Engine &e, LuaBoard &b, sol::table limits) -> double {
            Analysis a = e.analyse(b.board.getFen(), helper_readLimits(limits));
            double cp = 0.0;
            if (a.mate) cp = (*a.mate >= 0 ? 1.0 : -1.0) * 100000.0;
            else cp = a.score.value_or(0.0);
            if (b.board.sideToMove() == chess::Color::BLACK) cp = -cp;
            return cp;
        }
    );
}

void registerTypes_Args(sol::state_view lua) {
    lua.new_usertype<Args>(
        "Args", sol::no_constructor,
        "get", [](Args &a, std::string const &key, sol::optional<sol::object> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = helper_findNonEmptyArgs(a.values, key);
            if (found.empty()) {
                if (def) return *def;
                return sol::lua_nil;
            }
            return helper_scalarOrArray(ts, found, [](std::string const &s) { return s; });
        },
        "number", [](Args &a, std::string const &key, sol::optional<double> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = helper_findNonEmptyArgs(a.values, key);
            if (found.empty()) {
                if (def) return sol::make_object(ts, *def);
                return sol::lua_nil;
            }
            return helper_scalarOrArrayChecked(ts, key, "number", found, [](std::string const &s) -> std::optional<double> {
                double value = 0.0;
                auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
                if (ec != std::errc{}) return std::nullopt;
                return value;
            });
        },
        "bool", [](Args &a, std::string const &key, sol::optional<bool> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = helper_findArgs(a.values, key);
            if (found.empty()) {
                if (def) return sol::make_object(ts, *def);
                return sol::lua_nil;
            }
            return helper_scalarOrArrayChecked(ts, key, "boolean", found, [](std::string const &s) -> std::optional<bool> {
                if (s.empty()) return false; // `foo=` explicitly disables the flag
                constexpr std::array<std::string_view, 4> truthy{"true", "1", "yes", "on"};
                constexpr std::array<std::string_view, 4> falsy{"false", "0", "no", "off"};
                if (std::ranges::contains(truthy, s)) return true;
                if (std::ranges::contains(falsy, s)) return false;
                return std::nullopt;
            });
        },
        "require", [](Args &a, std::string const &key, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = helper_findNonEmptyArgs(a.values, key);
            if (found.empty()) throw std::runtime_error("missing required argument: " + key);
            return helper_scalarOrArray(ts, found, [](std::string const &s) { return s; });
        },
        "list", [](Args &a, std::string const &key, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = helper_findNonEmptyArgs(a.values, key);
            if (found.empty()) return sol::lua_nil;
            sol::table out = sol::state_view(ts).create_table();
            int n = 0;
            for (auto const *v : found) {
                std::stringstream ss(*v);
                std::string item;
                while (std::getline(ss, item, ',')) out[++n] = item;
            }
            return out;
        }
    );
}
// clang-format on

} // namespace

void Runtime::registerTypes() {
    registerTypes_Move(lua_);
    registerTypes_Board(lua_);
    registerTypes_Game(lua_);
    registerTypes_Writer(lua_);
    registerTypes_Engine(lua_);
    registerTypes_Args(lua_);
}

void Runtime::loadPlugins() {
    for (auto const &spec : opts_.plugins) {
        sol::protected_function_result loaded = lua_.safe_script_file(spec.path, sol::script_pass_on_error);
        if (!loaded.valid())
            throw std::runtime_error("failed to load '" + spec.path + "': " + sol::error(loaded).what());

        sol::object obj = loaded;
        if (!obj.is<sol::table>())
            throw std::runtime_error("plugin '" + spec.path + "' did not return a table");

        auto plugin = std::make_unique<PluginInstance>();
        plugin->table = obj.as<sol::table>();
        if (!plugin->table["process"].is<sol::protected_function>())
            throw std::runtime_error("plugin '" + spec.path + "' must define a 'process' function");

        plugin->args = std::make_shared<Args>();
        plugin->args->values = spec.args;
        plugin->name = spec.path;

        if (plugin->table["meta"].valid()) {
            sol::table meta = plugin->table["meta"];
            if (meta["name"].valid()) plugin->name = meta.get<std::string>("name");
        }

        plugin->ctx = buildCtx(*plugin);
        plugins_.push_back(std::move(plugin));
    }
}

sol::table Runtime::buildCtx(PluginInstance &plugin) {
    PluginInstance *self = &plugin;
    sol::state_view lua = lua_;
    sol::table ctx = lua.create_table();

    ctx["args"] = plugin.args;
    ctx["shared"] = shared_;
    ctx["scope"] = lua.create_table();
    ctx["out"] = out_;
    ctx["engine"] = [self](std::string const &path, sol::optional<sol::table> opts) {
        auto it = self->engines.find(path);
        if (it != self->engines.end()) return it->second;
        std::unordered_map<std::string, std::string> options;
        if (opts) {
            for (auto &kv : *opts) {
                std::string key = kv.first.as<std::string>();
                sol::object value = kv.second;
                if (value.is<std::string>()) options[key] = value.as<std::string>();
                else if (value.is<bool>()) options[key] = value.as<bool>() ? "true" : "false";
                else options[key] = std::to_string(value.as<double>());
            }
        }
        auto eng = std::make_shared<Engine>(path, options);
        self->engines[path] = eng;
        return eng;
    };
    ctx["open"] = [self](std::string const &path, sol::optional<std::string> mode) {
        auto w = std::make_shared<Writer>(path, mode.value_or("w") == "a");
        self->managed.push_back(w);
        return w;
    };

    std::string const name = plugin.name;
    sol::table log = lua.create_table();
    auto const logger = [this, name](char const *level) {
        return [this, name, level](sol::this_state ts, std::string const &fmt, sol::variadic_args va) {
            sol::state_view l(ts);
            sol::protected_function format = l["string"]["format"];
            sol::protected_function_result r = format(fmt, va);
            std::string msg = r.valid() ? r.get<std::string>() : fmt;
            std::println(stderr, "[{}:{}] {}: {}", name, current_index_, level, msg);
        };
    };
    log["info"] = logger("info");
    log["warn"] = logger("warn");
    log["error"] = logger("error");
    log["debug"] = logger("debug");
    ctx["log"] = log;

    return ctx;
}

namespace {

// Outcome of interpreting one plugin return value (see LUA.md §4).
struct ProcessResult {
    std::vector<PluginValue> values;  // values forwarded to the next plugin
    bool stop = false;                // graceful stop: emit, then stop reading the database
    bool abort = false;               // hard abort: drop the in-flight game and unwind
    std::optional<std::string> error; // set when the return value is invalid
};

// Return the first key of `table` not present in `allowed`, or `std::nullopt`
// if all are allowed. `allowArray` skips integer keys, for the top-level
// fan-out table whose array *is* the payload; everywhere else a positional
// entry is itself an unsupported key.
std::optional<std::string> processGame_findUnknownKey(
    sol::table table, std::initializer_list<char const *> allowed, bool allowArray = false
) {
    for (auto const &kv : table) {
        if (!kv.first.is<std::string>()) {
            if (allowArray) continue;
            return "positional entry";
        }
        std::string const key = kv.first.as<std::string>();
        if (std::ranges::find(allowed, key) == allowed.end()) return key;
    }
    return std::nullopt;
}

// A table with no fields at all conveys no information; reject it rather than
// silently treating it as an all-defaults pass-through.
bool processGame_tableIsEmpty(sol::table table) { return table.begin() == table.end(); }

// Extract the `action` field as a string (defaulting to "pass"), or nullopt if
// it is present but not a string -- sol2's unchecked get<std::string>() would
// otherwise abort the process on a type mismatch instead of surfacing a plugin
// error.
std::optional<std::string> processGame_getAction(sol::table table) {
    sol::object const action = table["action"];
    if (!action.valid()) return "pass";
    if (!action.is<std::string>()) return std::nullopt;
    return action.as<std::string>();
}

// Reject `game`/`board` fields whose value isn't the type processGame_valueFrom
// expects, for the same reason as processGame_getAction above.
std::optional<std::string> processGame_checkFieldTypes(sol::table table) {
    sol::object const game = table["game"];
    if (game.valid() && !game.is<LuaGame>()) return "field 'game' is not a Game";
    sol::object const board = table["board"];
    if (board.valid() && !board.is<LuaBoard>()) return "field 'board' is not a Board";
    return std::nullopt;
}

// Build an output value from `base`, overlaying the game/board/data fields
// present in `table`.
PluginValue processGame_valueFrom(sol::table table, PluginValue const &base) {
    PluginValue out = base;
    if (table["game"].valid()) out.game = table.get<LuaGame>("game").g;
    if (table["board"].valid()) out.board = table.get<LuaBoard>("board").board;
    if (table["data"].valid()) out.data = table.get<sol::object>("data");
    return out;
}

ProcessResult processGame_interpret(sol::object ret, PluginValue const &deflt) {
    // bare `return` / nil -> drop, same as `return false`
    if (ret == sol::lua_nil) return {};

    if (ret.is<bool>()) {
        if (!ret.as<bool>()) return {};          // false -> drop
        return {.values = {deflt}, .error = {}}; // true -> pass
    }

    if (ret.is<LuaBoard>()) {
        PluginValue out = deflt;
        out.board = ret.as<LuaBoard>().board;
        return {.values = {out}, .error = {}};
    }

    if (!ret.is<sol::table>()) {
        return {.values = {}, .error = "bad return value: expected nil, boolean, Board, or table"};
    }

    sol::table table = ret.as<sol::table>();

	// Fan-out table.
	if (table[1].is<sol::table>()) {
        // Top-level action (default "pass") plus an array of value tables.
        if (auto key = processGame_findUnknownKey(table, {"action"}, true)) {
            return {.values = {}, .error = "returned a fan-out with an unsupported key '" + *key + "'"};
        }
        auto actionOpt = processGame_getAction(table);
        if (!actionOpt) {
            return {.values = {}, .error = "returned a fan-out with a non-string 'action'"};
        }
        std::string const action = *actionOpt;
        if (action != "pass" && action != "stop") {
            return {
                .values = {},
                .error = "returned a fan-out with action '" + action +
                         "'; a fan-out only accepts action 'pass' or 'stop'"
            };
        }
        std::size_t const n = table.size();
        for (std::size_t i = 1; i <= n; ++i) {
            sol::object elem = table[i];
            if (!elem.is<sol::table>()) {
                return {
                    .values = {},
                    .error = "returned a fan-out whose element #" + std::to_string(i) + " is not a table"
                };
            }
            sol::table const elemTable = elem.as<sol::table>();
            if (processGame_tableIsEmpty(elemTable)) {
                return {
                    .values = {},
                    .error = "returned a fan-out whose element #" + std::to_string(i) + " is empty"
                };
            }
            if (auto key = processGame_findUnknownKey(elemTable, {"game", "board", "data"})) {
                return {
                    .values = {},
                    .error = "returned a fan-out element with an unsupported key '" + *key + "'"
                };
            }
            if (auto err = processGame_checkFieldTypes(elemTable)) {
                return {.values = {}, .error = "returned a fan-out element whose " + *err};
            }
        }
        std::vector<PluginValue> values;
        values.reserve(n);
        for (std::size_t i = 1; i <= n; ++i)
            values.push_back(processGame_valueFrom(table.get<sol::table>(i), deflt));
        return {.values = std::move(values), .stop = action == "stop", .error = {}};
    }

    // Single result table.
    if (processGame_tableIsEmpty(table)) {
        return {.values = {}, .error = "returned an empty table"};
    }
    if (auto key = processGame_findUnknownKey(table, {"action", "game", "board", "data"})) {
        return {.values = {}, .error = "returned a result table with an unsupported key '" + *key + "'"};
    }
    if (auto err = processGame_checkFieldTypes(table)) {
        return {.values = {}, .error = "returned a result table whose " + *err};
    }
    auto actionOpt = processGame_getAction(table);
    if (!actionOpt) {
        return {.values = {}, .error = "returned a result table with a non-string 'action'"};
    }
    std::string const action = *actionOpt;
    if (action == "drop") return {}; // emit nothing
    if (action == "abort") {
        // drop the in-flight game and unwind the pipeline
        return {.values = {}, .abort = true, .error = {}};
    }
    if (action == "pass" || action == "stop") {
        return {.values = {processGame_valueFrom(table, deflt)}, .stop = action == "stop", .error = {}};
    }
    return {.values = {}, .error = "returned a result table with unknown action '" + action + "'"};
}

// Handle a plugin error under the configured policy: throw (Abort) so the
// pipeline unwinds, or log it and report whether the failed value should still
// pass through.
inline bool
processGame_onError(OnError onError, std::string_view name, std::size_t index, std::string_view msg) {
    if (onError == OnError::Abort)
        throw std::runtime_error(std::format("[{}:{}] error: {}", name, index, msg));
    std::println(stderr, "[{}:{}] error: {}", name, index, msg);
    return onError == OnError::Pass;
}

} // namespace

bool Runtime::processGame(std::shared_ptr<Game> const &game, std::size_t index) {
    bool stop = false;  // finish this game, then stop reading the database (graceful)
    bool abort = false; // stop immediately: skip the rest of the pipeline for this game

    // The values threaded through the plugin chain: starts as just the game,
    // and each plugin's process() can replace, drop, or fan it out before the
    // next plugin sees it.
    std::vector<PluginValue> values = {{game, game->boardAt(-1), sol::object{}}};

    for (auto &plugin : plugins_) {
        if (values.empty()) break;

        std::vector<PluginValue> next;
        plugin->ctx["index"] = index;

        for (auto &value : values) {
            sol::table input = lua_.create_table();
            input["game"] = LuaGame{value.game};
            input["board"] = LuaBoard{value.board};
            input["data"] = value.data;

            auto res = plugin->table["process"](input, plugin->ctx);
            if (!res.valid()) {
                if (processGame_onError(opts_.onError, plugin->name, index, sol::error(res).what()))
                    next.push_back(value);
                continue;
            }

            ProcessResult result = processGame_interpret(res, value);
            if (result.error) {
                if (processGame_onError(opts_.onError, plugin->name, index, *result.error))
                    next.push_back(value);
                continue;
            }

            for (auto &v : result.values) next.push_back(std::move(v));
            if (result.stop) stop = true;
            if (result.abort) {
                abort = true;
                break;
            }
        }
        if (abort) break;
        values = std::move(next);
    }

    // A hard abort drops the in-flight game; a graceful stop still emits it.
    if (!abort && out_) {
        for (auto &val : values) out_->writeGame(val.game);
    }
    return stop || abort;
}

PluginSpec parsePluginSpec(std::string const &spec) {
    PluginSpec plugin;
    std::istringstream iss(spec);
    for (std::string tok; iss >> tok;) {
        if (plugin.path == "") {
            plugin.path = tok;
            continue;
        }
        auto eq = tok.find('=');
        if (eq == std::string::npos) {
            plugin.args.emplace_back(tok, "true");
        } else {
            plugin.args.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
        }
    }
    if (plugin.path.empty()) throw std::runtime_error("empty plugin spec");
    return plugin;
}

} // namespace tictac
