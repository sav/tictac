// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: sol2 bindings and pipeline execution.

#include "runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <sstream>
#include <stdexcept>

namespace tictac {

namespace {

namespace detail {

std::optional<chess::Move> parseMove(chess::Board const &board, std::string_view move) {
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

LuaMove makeLuaMove(
    chess::Move mv, chess::Board const &before, std::shared_ptr<Game> game = nullptr, int ply = -1
) {
    return LuaMove{mv, before, std::move(game), ply};
}

std::string squareStr(chess::Square sq) { return static_cast<std::string>(sq); }

sol::object pieceAt(sol::this_state ts, chess::Board const &board, std::string const &sq) {
    if (!chess::Square::is_valid_string_sq(sq)) return sol::lua_nil;
    chess::Piece p = board.at(chess::Square(sq));
    if (p == chess::Piece::NONE) return sol::lua_nil;
    return sol::make_object(ts, static_cast<std::string>(p));
}

int pieceValue(chess::PieceType pt) {
    switch (pt.internal()) {
    case chess::PieceType::PAWN: return 100;
    case chess::PieceType::KNIGHT: return 320;
    case chess::PieceType::BISHOP: return 330;
    case chess::PieceType::ROOK: return 500;
    case chess::PieceType::QUEEN: return 900;
    default: return 0;
    }
}

sol::table analysisToTable(sol::state_view lua, Analysis const &a) {
    sol::table table = lua.create_table();
    if (a.score) table["score"] = *a.score;
    if (a.mate) table["mate"] = *a.mate;
    table["depth"] = a.depth;
    table["seldepth"] = a.seldepth;
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

// A Lua number always reaches C++ as a double -- ctx.args:number() yields one,
// and even an integer literal converts -- so whole-number values are read as
// doubles and validated here, rejecting a fractional one rather than letting
// sol2 refuse the float outright. `prefix` namespaces the message (e.g.
// "engine: ") when the caller wants it.
template <typename Int>
Int requireWhole(double value, char const *name, char const *prefix = "") {
    if (value != std::trunc(value))
        throw std::runtime_error(std::format("{}'{}' must be a whole number", prefix, name));
    return static_cast<Int>(value);
}

// A whole-number call argument; absent falls back to the default.
int readIntArg(sol::optional<double> value, int deflt, char const *name) {
    if (!value) return deflt;
    return requireWhole<int>(*value, name);
}

// A whole-number engine limit read from `table` by key; absent is an error.
template <typename Int>
Int readIntLimit(sol::table const &table, char const *key) {
    sol::optional<double> const value = table.get<sol::optional<double>>(key);
    if (!value) throw std::runtime_error(std::format("engine: '{}' must be a number", key));
    return requireWhole<Int>(*value, key, "engine: ");
}

AnalysisLimits readLimits(sol::table const &table) {
    AnalysisLimits lim;
    if (table["depth"].valid()) lim.depth = readIntLimit<int>(table, "depth");
    if (table["movetime"].valid()) lim.movetime = readIntLimit<int>(table, "movetime");
    if (table["nodes"].valid()) lim.nodes = readIntLimit<std::int64_t>(table, "nodes");
    if (table["multipv"].valid()) lim.multipv = readIntLimit<int>(table, "multipv");
    return lim;
}

// UCI sends option values as bare text, so a Lua string, number or boolean all
// have to reach the engine spelled the way UCI expects. Integers in particular
// must not go out as std::to_string's "16.000000".
std::string uciOptionValue(sol::object const &value) {
    if (value.is<std::string>()) return value.as<std::string>();
    if (value.is<bool>()) return value.as<bool>() ? "true" : "false";
    double const d = value.as<double>();
    return d == std::trunc(d) ? std::format("{}", static_cast<std::int64_t>(d)) : std::format("{}", d);
}

// All values stored for `key`, in the order they were passed (empty if none).
std::vector<std::string const *>
findArgs(std::vector<std::pair<std::string, std::string>> const &values, std::string_view key) {
    std::vector<std::string const *> out;
    for (auto const &[k, v] : values)
        if (k == key) out.push_back(&v);
    return out;
}

// Like findArgs, but drops entries with an explicitly empty value:
// `key=` is treated the same as `key` being absent. Every accessor except
// `bool` wants this -- `bool` treats an empty value as an explicit false
// instead.
std::vector<std::string const *>
findNonEmptyArgs(std::vector<std::pair<std::string, std::string>> const &values, std::string_view key) {
    std::vector<std::string const *> out = findArgs(values, key);
    std::erase_if(out, [](std::string const *v) { return v->empty(); });
    return out;
}

// Coerce the found values with `f`: a lone match yields the scalar, several
// yield a 1-based array in argument order. `found` must be non-empty.
template <typename F>
sol::object scalarOrArray(sol::this_state ts, std::vector<std::string const *> const &found, F f) {
    if (found.size() == 1) return sol::make_object(ts, f(*found.front()));
    sol::table out = sol::state_view(ts).create_table();
    for (std::size_t i = 0; i < found.size(); ++i) out[i + 1] = f(*found[i]);
    return out;
}

// Like scalarOrArray, but `f` may reject a value (returning nullopt). On
// the first rejection, print an error naming `key`, `typeName` (the expected
// type), and the offending value to stderr and return nil for the whole call.
// `found` must be non-empty.
template <typename F>
sol::object scalarOrArrayChecked(
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

// Opening the output truncates it, and that happens before run() reads any
// input, so an --output that names an --file would destroy the database before
// it is parsed. Refuse instead.
void rejectOutputOverInput(RunOptions const &opts) {
    if (opts.noOutput || opts.output == "-") return;
    std::error_code ec;
    for (auto const &file : opts.files) {
        if (file == "-") continue;
        // equivalent() fails when the output does not exist yet, which is the
        // common case and is not a collision.
        if (std::filesystem::equivalent(file, opts.output, ec))
            throw std::runtime_error("refusing to write output over input file: " + file);
    }
}

} // namespace detail

} // namespace

Runtime::Runtime(RunOptions opts) : opts_(std::move(opts)) {
    detail::rejectOutputOverInput(opts_);
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
                return 1; // init failure means the plugin can't run; always abort, regardless
                          // of --on-error
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
        // Games stream through the pipeline one at a time: the parser hands over
        // each game as it completes, so a database never sits in memory whole.
        parseGames(in, [&](std::shared_ptr<Game> const &game) {
            ++index;
            current_index_ = index;
            if (processGame(game, index)) aborted = true;
            return !aborted;
        });
    }
    // Call finish() in pipeline order. Unlike init(), a failure is only reported:
    // the games are already emitted, and one bad plugin must not skip the rest.
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
    for (auto &plugin : plugins_) plugin->engines.clear();
    return 0;
}

namespace {

namespace detail {

// clang-format off
void registerMove(sol::state_view lua) {
    lua.new_usertype<LuaMove>(
        "Move", sol::no_constructor,
        "san", [](LuaMove &m) { return chess::uci::moveToSan(m.before, m.move); },
        "uci", [](LuaMove &m) { return chess::uci::moveToUci(m.move); },
        "from", [](LuaMove &m) { return squareStr(m.move.from()); },
        "to", [](LuaMove &m) { return squareStr(m.move.to()); },
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

void registerBoard(sol::state_view lua) {
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
            for (auto const &m : moves) out.push_back(makeLuaMove(m, b.board));
            return sol::as_table(out);
        },
        "isLegal", [](LuaBoard &b, std::string const &mv) { return parseMove(b.board, mv).has_value(); },
        "makeMove", [](LuaBoard &b, std::string const &mv) {
            auto parsed = parseMove(b.board, mv);
            if (!parsed) throw std::runtime_error("illegal move: " + mv);
            LuaBoard nb{b.board};
            nb.board.makeMove(*parsed);
            return nb;
        },
        "piece", [](LuaBoard &b, std::string const &sq, sol::this_state ts) { return pieceAt(ts, b.board, sq); },
        "pieces", [](LuaBoard &b, sol::optional<sol::table> filter, sol::this_state ts) {
            sol::table out = sol::state_view(ts).create_table();
            std::optional<chess::Color> fc;
            std::optional<chess::PieceType> ft;
            if (filter) {
                sol::table f = *filter;
                if (f["color"].valid()) {
                    std::string const color = f.get<std::string>("color");
                    if (color == "white") fc = chess::Color::WHITE;
                    else if (color == "black") fc = chess::Color::BLACK;
                    else throw std::runtime_error("pieces: color must be 'white' or 'black', got '" + color + "'");
                }
                if (f["type"].valid()) {
                    std::string const type = f.get<std::string>("type");
                    ft = chess::PieceType(type);
                    if (*ft == chess::PieceType::NONE)
                        throw std::runtime_error("pieces: unknown piece type '" + type + "'");
                }
            }
            for (int i = 0; i < 64; ++i) {
                chess::Square sq(i);
                chess::Piece p = b.board.at(sq);
                if (p == chess::Piece::NONE) continue;
                if (fc && p.color() != *fc) continue;
                if (ft && p.type() != *ft) continue;
                out[squareStr(sq)] = static_cast<std::string>(p);
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
        "isRepetition", [](LuaBoard &b, sol::optional<double> count) { return b.board.isRepetition(readIntArg(count, 2, "count")); },
        "phase",
        [](LuaBoard &b, sol::optional<double> openingMoves, sol::optional<double> endgameThreshold) {
            int const opening = readIntArg(openingMoves, 10, "openingMoves");
            int const endgame = readIntArg(endgameThreshold, 1300, "endgameThreshold");
            int total = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                if (p.type() == chess::PieceType::PAWN || p.type() == chess::PieceType::KING) continue;
                total += pieceValue(p.type());
            }
            if (static_cast<int>(b.board.fullMoveNumber()) <= opening) return std::string("opening");
            if (total <= endgame) return std::string("endgame");
            return std::string("middlegame");
        },
        "material", [](LuaBoard &b, sol::this_state ts) {
            int white = 0, black = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                int v = pieceValue(p.type());
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

void registerGame(sol::state_view lua) {
    lua.new_usertype<LuaGame>(
        "Game", sol::no_constructor,
        "header", [](LuaGame &g, std::string const &key, sol::this_state ts) -> sol::object {
            std::optional<std::string_view> v = g.g->findHeader(key);
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
                out.push_back(makeLuaMove(g.g->moves[i].move, board, g.g, static_cast<int>(i)));
                board.makeMove(g.g->moves[i].move);
            }
            return sol::as_table(out);
        },
        "startBoard", [](LuaGame &g) { return LuaBoard{g.g->startBoard()}; },
        "board", [](LuaGame &g, sol::optional<double> ply) { return LuaBoard{g.g->boardAt(readIntArg(ply, 0, "ply"))}; },
        // Returns a single-use iterator: idx and the running board live in the
        // closure (so the walk stays linear rather than replaying from the start
        // each step), and game is captured by shared_ptr because the iterator
        // outlives this LuaGame.
        "positions", [](LuaGame &g, sol::this_state ts) {
            sol::state_view l(ts);
            auto game = g.g;
            auto idx = std::make_shared<std::size_t>(0);
            auto cursor = std::make_shared<chess::Board>(game->startBoard());
            return sol::make_object(
                l, std::function<sol::object(sol::this_state)>([game, idx, cursor](sol::this_state s) -> sol::object {
                    if (*idx >= game->moves.size()) return sol::lua_nil;
                    std::size_t i = *idx;
                    chess::Board before = *cursor;
                    chess::Board after = before;
                    after.makeMove(game->moves[i].move);
                    sol::table node = sol::state_view(s).create_table();
                    node["ply"] = i + 1;
                    node["move"] = makeLuaMove(game->moves[i].move, before, game, static_cast<int>(i));
                    node["board_before"] = LuaBoard{before};
                    node["board_after"] = LuaBoard{after};
                    node["board"] = LuaBoard{after};
                    *cursor = after;
                    ++(*idx);
                    return node;
                }));
        },
        "pgn", [](LuaGame &g) { return g.g->pgn(); },
        "clone", [](LuaGame &g) { return LuaGame{g.g->clone()}; },
        sol::meta_function::to_string, [](LuaGame &g) {
            std::optional<std::string_view> white = g.g->findHeader("White");
            std::optional<std::string_view> black = g.g->findHeader("Black");
            return std::format("{} vs {}, {}", white.value_or("?"), black.value_or("?"), g.g->result());
        }
    );
}

void registerWriter(sol::state_view lua) {
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

void registerEngine(sol::state_view lua) {
    lua.new_usertype<Engine>(
        "Engine", sol::no_constructor,
        "setOption", [](Engine &e, std::string const &name, sol::object value) {
            e.setOption(name, uciOptionValue(value));
        },
        "analyse", [](Engine &e, LuaBoard &b, sol::table limits, sol::this_state ts) {
            sol::state_view l(ts);
            return analysisToTable(l, e.analyse(b.board.getFen(), readLimits(limits)));
        },
        "bestmove", [](Engine &e, LuaBoard &b, sol::table limits) {
            return e.analyse(b.board.getFen(), readLimits(limits)).bestmove;
        },
        "cp", [](Engine &e, LuaBoard &b, sol::table limits) -> double {
            // UCI scores are side-to-move relative; report them White-positive,
            // with mate flattened to a sentinel that orders past any centipawn.
            Analysis a = e.analyse(b.board.getFen(), readLimits(limits));
            double cp = 0.0;
            if (a.mate) cp = (*a.mate >= 0 ? 1.0 : -1.0) * 100000.0;
            else cp = a.score.value_or(0.0);
            if (b.board.sideToMove() == chess::Color::BLACK) cp = -cp;
            return cp;
        }
    );
}

void registerArgs(sol::state_view lua) {
    lua.new_usertype<Args>(
        "Args", sol::no_constructor,
        "get", [](Args &a, std::string const &key, sol::optional<sol::object> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = findNonEmptyArgs(a.values, key);
            if (found.empty()) {
                if (def) return *def;
                return sol::lua_nil;
            }
            return scalarOrArray(ts, found, [](std::string const &s) { return s; });
        },
        "number", [](Args &a, std::string const &key, sol::optional<double> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = findNonEmptyArgs(a.values, key);
            if (found.empty()) {
                if (def) return sol::make_object(ts, *def);
                return sol::lua_nil;
            }
            return scalarOrArrayChecked(ts, key, "number", found, [](std::string const &s) -> std::optional<double> {
                double value = 0.0;
                auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
                if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
                return value;
            });
        },
        "bool", [](Args &a, std::string const &key, sol::optional<bool> def, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = findArgs(a.values, key);
            if (found.empty()) {
                if (def) return sol::make_object(ts, *def);
                return sol::lua_nil;
            }
            return scalarOrArrayChecked(ts, key, "boolean", found, [](std::string const &s) -> std::optional<bool> {
                if (s.empty()) return false; // `foo=` explicitly disables the flag
                constexpr std::array<std::string_view, 4> truthy{"true", "1", "yes", "on"};
                constexpr std::array<std::string_view, 4> falsy{"false", "0", "no", "off"};
                if (std::ranges::contains(truthy, s)) return true;
                if (std::ranges::contains(falsy, s)) return false;
                return std::nullopt;
            });
        },
        "require", [](Args &a, std::string const &key, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = findNonEmptyArgs(a.values, key);
            if (found.empty()) throw std::runtime_error("missing required argument: " + key);
            return scalarOrArray(ts, found, [](std::string const &s) { return s; });
        },
        "list", [](Args &a, std::string const &key, sol::this_state ts) -> sol::object {
            std::vector<std::string const *> found = findNonEmptyArgs(a.values, key);
            if (found.empty()) return sol::lua_nil;
            sol::table out = sol::state_view(ts).create_table();
            int n = 0;
            for (auto const *v : found) {
                std::stringstream ss(*v);
                std::string item;
                while (std::getline(ss, item, ',')) out[++n] = item;
            }
            return out;
        },
        "each", [](Args &a, sol::this_state ts) {
            sol::state_view lua(ts);
            sol::table out = lua.create_table();
            int n = 0;
            for (auto const &[k, v] : a.values) {
                sol::table entry = lua.create_table();
                entry["key"] = k;
                entry["value"] = v;
                out[++n] = entry;
            }
            return out;
        }
    );
}
// clang-format on

} // namespace detail

} // namespace

void Runtime::registerTypes() {
    detail::registerMove(lua_);
    detail::registerBoard(lua_);
    detail::registerGame(lua_);
    detail::registerWriter(lua_);
    detail::registerEngine(lua_);
    detail::registerArgs(lua_);
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
    ctx["shared"] = shared_;           // one table for every plugin
    ctx["scope"] = lua.create_table(); // fresh per plugin, so keys cannot collide
    ctx["out"] = out_;
    ctx["engine"] = [self](std::string const &path, sol::optional<sol::table> opts) {
        auto it = self->engines.find(path);
        if (it != self->engines.end()) return it->second;
        std::unordered_map<std::string, std::string> options;
        if (opts) {
            for (auto &kv : *opts) options[kv.first.as<std::string>()] = detail::uciOptionValue(kv.second);
        }
        auto eng = std::make_shared<Engine>(path, options);
        self->engines[path] = eng;
        return eng;
    };
    ctx["open"] = [self](std::string const &path, sol::optional<std::string> mode) {
        // Reject anything but "w"/"a": silently truncating on a mode typo would
        // destroy the file the plugin meant to append to.
        std::string const m = mode.value_or("w");
        if (m != "w" && m != "a")
            throw std::runtime_error("open: mode must be 'w' or 'a', got '" + m + "'");
        auto w = std::make_shared<Writer>(path, m == "a");
        self->managed.push_back(w);
        return w;
    };

    std::string const name = plugin.name;
    sol::table log = lua.create_table();
    // this is captured so the prefix reports the game being processed at log
    // time, not the one current when the logger was built.
    auto const logger = [this, name](char const *level) {
        return [this, name, level](sol::this_state ts, std::string const &fmt, sol::variadic_args va) {
            sol::state_view l(ts);
            sol::protected_function format = l["string"]["format"];
            sol::protected_function_result r = format(fmt, va);
            // A bad format string must not vanish: report it alongside the raw text.
            std::string const msg = r.valid()
                                        ? r.get<std::string>()
                                        : std::format("{} [format error: {}]", fmt, sol::error(r).what());
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

namespace detail {

// A table with no fields at all conveys no information; reject it rather than
// silently treating it as an all-defaults pass-through.
bool tableIsEmpty(sol::table table) { return table.begin() == table.end(); }

// Return the first key of `table` not present in `allowed`, or `std::nullopt`
// if all are allowed. `allowArray` skips integer keys, for the top-level
// fan-out table whose array *is* the payload; everywhere else a positional
// entry is itself an unsupported key.
std::optional<std::string>
findUnknownKey(sol::table table, std::initializer_list<char const *> allowed, bool allowArray = false) {
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

// Extract the `action` field as a string (defaulting to "pass"), or nullopt if
// it is present but not a string -- sol2's unchecked get<std::string>() would
// otherwise abort the process on a type mismatch instead of surfacing a plugin
// error.
std::optional<std::string> getAction(sol::table table) {
    sol::object const action = table["action"];
    if (!action.valid()) return "pass";
    if (!action.is<std::string>()) return std::nullopt;
    return action.as<std::string>();
}

// Reject `game`/`board` fields whose value isn't of the expected type.
std::optional<std::string> checkFieldTypes(sol::table table) {
    if (table["game"].valid() && !table["game"].is<LuaGame>()) return "field 'game' is not a Game";
    if (table["board"].valid() && !table["board"].is<LuaBoard>()) return "field 'board' is not a Board";
    return std::nullopt;
}

// Build an output value from `base`, overlaying the game/board/data fields
// present in `table`.
PluginValue valueFrom(sol::table table, PluginValue const &deflt) {
    PluginValue base = deflt;
    if (table["game"].valid()) base.game = table.get<LuaGame>("game").g;
    if (table["board"].valid()) base.board = table.get<LuaBoard>("board").board;
    if (table["data"].valid()) base.data = table.get<sol::object>("data");
    return base;
}

// Interprets a single flat table. For nested fan-out tables see
// `interpretFanOut`.
ProcessResult interpretTable(sol::table const &table, PluginValue &&deflt) {
    if (tableIsEmpty(table)) return {.values = {}, .error = "returned an empty table"};
    if (auto key = findUnknownKey(table, {"action", "game", "board", "data"}))
        return {.values = {}, .error = "returned a result table with an unsupported key '" + *key + "'"};
    if (auto err = checkFieldTypes(table))
        return {.values = {}, .error = "returned a result table whose " + *err};
    auto actionOpt = getAction(table);
    if (!actionOpt) return {.values = {}, .error = "returned a result table with a non-string 'action'"};
    std::string const action = *actionOpt;
    if (action == "drop") return {};
    if (action == "abort") return {.values = {}, .abort = true, .error = {}};
    if (action != "stop" && action != "pass")
        return {.values = {}, .error = "returned a result table with unknown action '" + action + "'"};
    return {.values = {valueFrom(table, std::move(deflt))}, .stop = action == "stop", .error = {}};
}

// Validate a single element of the fan-out table. Return `false` and set
// `res->error` if it is malformed; `true` if it is well-formed.
bool validateFanOutElem(sol::object const &elem, std::size_t index, ProcessResult *res) {
    if (!elem.is<sol::table>()) {
        res->error = "returned a fan-out whose element #" + std::to_string(index) + " is not a table";
        return false;
    }
    if (tableIsEmpty(elem)) {
        res->error = "returned a fan-out whose element #" + std::to_string(index) + " is empty";
        return false;
    }
    if (auto key = findUnknownKey(elem, {"game", "board", "data"})) {
        res->error = "returned a fan-out element with an unsupported key '" + *key + "'";
        return false;
    }
    if (auto err = checkFieldTypes(elem)) {
        res->error = "returned a fan-out element whose " + *err;
        return false;
    }
    return true;
}

ProcessResult interpretFanOut(sol::table const &table, PluginValue const &deflt) {
    if (auto key = findUnknownKey(table, {"action"}, true))
        return {.values = {}, .error = "returned a fan-out table with an unsupported key '" + *key + "'"};
    auto actionOpt = getAction(table);
    if (!actionOpt) return {.values = {}, .error = "returned a fan-out with a non-string 'action'"};
    std::string const action = *actionOpt;
    if (action != "pass" && action != "stop") {
        return {
            .values = {},
            .error = "returned a fan-out with action '" + action +
                     "'; a fan-out only accepts action 'pass' or 'stop'"
        };
    }
    ProcessResult res = {.values = {}, .stop = action == "stop", .error = {}};
    std::size_t const n = table.size();
    res.values.reserve(n);
    for (std::size_t i = 1; i <= n; ++i) {
        if (!validateFanOutElem(table[i], i, &res)) return res;
        res.values.push_back(valueFrom(table[i], deflt));
    }
    return res;
}

ProcessResult interpret(sol::object ret, PluginValue const &deflt) {
    PluginValue value = deflt;

    switch (ret.get_type()) {
    case sol::type::none: [[fallthrough]];
    case sol::type::lua_nil: return {};
    case sol::type::boolean:
        return ret.as<bool>() ? ProcessResult{.values = {deflt}, .error = {}} : ProcessResult{};
    case sol::type::string: {
        std::string_view const action = ret.as<std::string_view>();
        if (action == "drop") return {};
        if (action == "abort") return {.values = {}, .abort = true, .error = {}};
        if (action == "pass" || action == "stop")
            return {.values = {deflt}, .stop = action == "stop", .error = {}};
        return {
            .values = {},
            .error = std::format(
                "returned an invalid action '{}'; expected 'pass', 'drop', 'stop', or 'abort'", action
            )
        };
    }
    case sol::type::table: {
        sol::table table = ret.as<sol::table>();
        // A table at index 1 is the only thing marking a fan-out; a flat table
        // with a stray positional entry is rejected by interpretTable instead.
        if (table[1].is<sol::table>()) return interpretFanOut(table, value);
        return interpretTable(table, std::move(value));
    }
    case sol::type::userdata: {
        if (ret.is<LuaGame>()) {
            // The board cursor is left as it was, so `return game` behaves the
            // same as `return { game = game }`.
            value.game = ret.as<LuaGame>().g;
        } else if (ret.is<LuaBoard>()) {
            value.board = ret.as<LuaBoard>().board;
        } else goto err;
        return {.values = {value}, .error = {}};
    }
    default: break;
    }
err:
    return {
        .values = {}, .error = "bad return value: expected nil, boolean, Board, Game, string or table."
    };
}

// Handle a plugin error under the configured policy: throw (Abort) so the
// pipeline unwinds, or log it and report whether the failed value should still
// pass through.
inline bool onError(OnError onError, std::string_view name, std::size_t index, std::string_view msg) {
    // The message is prefixed with "error: " by whoever reports it (App::run for
    // the thrown case), so it must not carry its own prefix.
    if (onError == OnError::Abort) throw std::runtime_error(std::format("[{}:{}] {}", name, index, msg));
    std::println(stderr, "error: [{}:{}] {}", name, index, msg);
    return onError == OnError::Pass;
}

} // namespace detail

} // namespace

bool Runtime::processGame(std::shared_ptr<Game> const &game, std::size_t index) {
    bool stop = false;  // finish this game, then stop reading the database (graceful)
    bool abort = false; // stop immediately: skip the rest of the pipeline for this game

    // The values threaded through the plugin chain: starts as just the game,
    // and each plugin's process() can replace, drop, or fan it out before the
    // next plugin sees it.
    std::vector<PluginValue> values = {{game, game->startBoard(), sol::object{}}};

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
                if (detail::onError(opts_.onError, plugin->name, index, sol::error(res).what()))
                    next.push_back(value);
                continue;
            }

            ProcessResult result = detail::interpret(res, value);
            if (result.error) {
                if (detail::onError(opts_.onError, plugin->name, index, *result.error))
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
    if (!abort && out_)
        for (auto &val : values) out_->writeGame(val.game);
    return stop || abort;
}

PluginSpec parsePluginSpec(std::string const &spec) {
    PluginSpec plugin;
    std::istringstream iss(spec);
    for (std::string tok; iss >> tok;) {
        if (plugin.path.empty()) {
            plugin.path = tok;
            continue;
        }
        // Whitespace tokenization means a value cannot contain a space. A bare
        // token becomes "true", and a repeated key is kept, not overwritten.
        auto eq = tok.find('=');
        if (eq == std::string::npos) plugin.args.emplace_back(tok, "true");
        else plugin.args.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
    }
    if (plugin.path.empty()) throw std::runtime_error("empty plugin spec");
    return plugin;
}

} // namespace tictac
