// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: sol2 bindings and pipeline execution.

#include "runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>
#include <ranges>
#include <sstream>
#include <stdexcept>

namespace tictac {

namespace {

std::optional<chess::Move> parseMove(chess::Board const &board, std::string_view move) {
    // try to parse `move` as SAN first; fall back to matching an UCI format if it fails.
    try {
        return chess::uci::parseSan(board, move);
    } catch (chess::uci::SanParseError const &) {
    } catch (chess::uci::AmbiguousMoveError const &) {}
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (auto const &m : moves) {
        if (chess::uci::moveToUci(m) == move) return m;
    }
    return std::nullopt;
}

LuaMove makeLuaMove(chess::Move mv, chess::Board const &before, std::shared_ptr<Game> game = nullptr, int ply = -1) {
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
    sol::table t = lua.create_table();
    if (a.score) t["score"] = *a.score;
    if (a.mate) t["mate"] = *a.mate;
    t["depth"] = a.depth;
    t["nodes"] = a.nodes;
    t["time"] = a.time;
    t["nps"] = a.nps;
    t["bestmove"] = a.bestmove;
    sol::table pv = lua.create_table();
    for (std::size_t i = 0; i < a.pv.size(); ++i) pv[i + 1] = a.pv[i];
    t["pv"] = pv;
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
        t["lines"] = lines;
    }
    return t;
}

AnalysisLimits readLimits(sol::table const &t) {
    AnalysisLimits lim;
    if (t["depth"].valid()) lim.depth = t.get<int>("depth");
    if (t["movetime"].valid()) lim.movetime = t.get<int>("movetime");
    if (t["nodes"].valid()) lim.nodes = t.get<std::int64_t>("nodes");
    if (t["multipv"].valid()) lim.multipv = t.get<int>("multipv");
    return lim;
}

// All values stored for `key`, in the order they were passed (empty if none).
std::vector<std::string const *>
findArgs(std::vector<std::pair<std::string, std::string>> const &values, std::string_view key) {
    std::vector<std::string const *> out;
    for (auto const &[k, v] : values)
        if (k == key) out.push_back(&v);
    return out;
}

// Like findArgs, but drops entries with an explicitly empty value: `key=` is
// treated the same as `key` being absent. Every accessor except `bool` wants
// this -- `bool` treats an empty value as an explicit false instead.
std::vector<std::string const *>
findNonEmptyArgs(std::vector<std::pair<std::string, std::string>> const &values, std::string_view key) {
    std::vector<std::string const *> out = findArgs(values, key);
    std::erase_if(out, [](std::string const *v) { return v->empty(); });
    return out;
}

// Coerce the found values with `f`: a lone match yields the scalar, several yield
// a 1-based array in argument order. `found` must be non-empty.
template <typename F>
sol::object scalarOrArray(sol::this_state ts, std::vector<std::string const *> const &found, F f) {
    if (found.size() == 1) return sol::make_object(ts, f(*found.front()));
    sol::state_view lua(ts);
    sol::table out = lua.create_table();
    for (std::size_t i = 0; i < found.size(); ++i) out[i + 1] = f(*found[i]);
    return out;
}

// Like scalarOrArray, but `f` may reject a value (returning nullopt). On the first
// rejection, print an error naming `key`, `typeName` (the expected type), and the
// offending value to stderr and return nil for the whole call. `found` must be
// non-empty.
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
            std::println(stderr, "error: invalid value '{}' for argument '{}': expected a {}", *v, key, typeName);
            return sol::lua_nil;
        }
        values.push_back(sol::make_object(ts, *coerced));
    }
    if (values.size() == 1) return values.front();
    sol::state_view lua(ts);
    sol::table out = lua.create_table();
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

    if (!opts_.noOutput) {
        out_ = opts_.output == "-" ? std::make_shared<Writer>() : std::make_shared<Writer>(opts_.output);
    }

    registerTypes();
    loadPlugins();
}

int Runtime::run() {
    // Call init() for every plugin.
    for (auto &inst : plugins_) {
        sol::optional<sol::protected_function> init = inst->plugin["init"];
        if (init) {
            sol::protected_function_result r = (*init)(inst->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::println(stderr, "error: init of '{}': {}", inst->name, err.what());
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
    for (auto &inst : plugins_) {
        sol::optional<sol::protected_function> finish = inst->plugin["finish"];
        if (finish) {
            sol::protected_function_result r = (*finish)(inst->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::println(stderr, "error: finish of '{}': {}", inst->name, err.what());
            }
        }
    }

    // Drop the runtime's cached engine handles; ~Engine quits and reaps each
    // subprocess once its last reference (the Lua-held copy included) is gone.
    for (auto &inst : plugins_) {
        inst->engines.clear();
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
            sol::state_view l(ts);
            sol::table t = l.create_table();
            if (m.game && m.ply >= 0) {
                auto const &n = m.game->moves[static_cast<std::size_t>(m.ply)].nags;
                for (std::size_t i = 0; i < n.size(); ++i) t[i + 1] = n[i];
            }
            return t;
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
            sol::state_view l(ts);
            sol::table out = l.create_table();
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
        "isRepetition", [](LuaBoard &b, sol::optional<int> count) { return b.board.isRepetition(count.value_or(2)); },
        "phase",
        [](LuaBoard &b, sol::optional<int> openingMoves, sol::optional<int> endgameThreshold) {
            int total = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                if (p.type() == chess::PieceType::PAWN || p.type() == chess::PieceType::KING) continue;
                total += pieceValue(p.type());
            }
            if (static_cast<int>(b.board.fullMoveNumber()) <= openingMoves.value_or(10)) return std::string("opening");
            if (total <= endgameThreshold.value_or(1300)) return std::string("endgame");
            return std::string("middlegame");
        },
        "material", [](LuaBoard &b, sol::this_state ts) {
            sol::state_view l(ts);
            int white = 0, black = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                int v = pieceValue(p.type());
                if (p.color() == chess::Color::WHITE) white += v;
                else black += v;
            }
            sol::table t = l.create_table();
            t["white"] = white;
            t["black"] = black;
            return t;
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
            sol::state_view l(ts);
            sol::table t = l.create_table();
            for (auto const &[k, v] : g.g->headers) t[k] = v;
            return t;
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
        "board", [](LuaGame &g, sol::optional<int> ply) { return LuaBoard{g.g->boardAt(ply.value_or(-1))}; },
        "positions", [](LuaGame &g, sol::this_state ts) {
            sol::state_view l(ts);
            auto game = g.g;
            auto idx = std::make_shared<std::size_t>(0);
            return sol::make_object(
                l, std::function<sol::object(sol::this_state)>([game, idx](sol::this_state s) -> sol::object {
                    sol::state_view lv(s);
                    if (*idx >= game->moves.size()) return sol::lua_nil;
                    std::size_t i = *idx;
                    chess::Board before = game->boardAt(static_cast<int>(i));
                    chess::Board after = before;
                    after.makeMove(game->moves[i].move);
                    sol::table node = lv.create_table();
                    node["ply"] = i + 1;
                    node["move"] = makeLuaMove(game->moves[i].move, before, game, static_cast<int>(i));
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
            return analysisToTable(l, e.analyse(b.board.getFen(), readLimits(limits)));
        },
        "bestmove", [](Engine &e, LuaBoard &b, sol::table limits) {
            return e.analyse(b.board.getFen(), readLimits(limits)).bestmove;
        },
        "cp", [](Engine &e, LuaBoard &b, sol::table limits) -> double {
            Analysis a = e.analyse(b.board.getFen(), readLimits(limits));
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
                if (ec != std::errc{}) return std::nullopt;
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
            sol::state_view l(ts);
            sol::table out = l.create_table();
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
    sol::state_view lua = lua_;
    registerTypes_Move(lua);
    registerTypes_Board(lua);
    registerTypes_Game(lua);
    registerTypes_Writer(lua);
    registerTypes_Engine(lua);
    registerTypes_Args(lua);
}

void Runtime::loadPlugins() {
    for (auto const &spec : opts_.plugins) {
        sol::protected_function_result loaded = lua_.safe_script_file(spec.path, sol::script_pass_on_error);
        if (!loaded.valid()) {
            sol::error err = loaded;
            throw std::runtime_error("failed to load plugin '" + spec.path + "': " + err.what());
        }
        sol::object obj = loaded;
        if (!obj.is<sol::table>()) {
            throw std::runtime_error("plugin '" + spec.path + "' did not return a table");
        }

        auto inst = std::make_unique<PluginInstance>();
        inst->plugin = obj.as<sol::table>();
        inst->args = std::make_shared<Args>();
        inst->args->values = spec.args;
        inst->name = spec.path;

        if (inst->plugin["meta"].valid()) {
            sol::table meta = inst->plugin["meta"];
            if (meta["name"].valid()) inst->name = meta.get<std::string>("name");
        }

        inst->ctx = buildCtx(*inst);
        plugins_.push_back(std::move(inst));
    }
}

sol::table Runtime::buildCtx(PluginInstance &inst) {
    PluginInstance *self = &inst;
    sol::state_view lua = lua_;
    sol::table ctx = lua.create_table();

    ctx["args"] = inst.args;
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
                sol::object val = kv.second;
                if (val.is<std::string>()) options[key] = val.as<std::string>();
                else if (val.is<bool>()) options[key] = val.as<bool>() ? "true" : "false";
                else options[key] = std::to_string(val.as<double>());
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

    std::string const name = inst.name;
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

bool Runtime::processGame(std::shared_ptr<Game> const &game, std::size_t index) {
    sol::state_view lua = lua_;
    bool stop = false;  // finish this game, then stop reading the database (graceful)
    bool abort = false; // stop immediately: skip the rest of the pipeline for this game

    // The values threaded through the plugin chain: starts as just the game, and each
    // plugin's process() can replace, drop, or fan it out before the next plugin sees it.
    std::vector<PluginValue> frontier;
    frontier.push_back(PluginValue{game, game->boardAt(-1), sol::object{}});

    for (auto &inst : plugins_) {
        if (frontier.empty()) break;
        inst->ctx["index"] = index;

        sol::optional<sol::protected_function> process = inst->plugin["process"];
        std::vector<PluginValue> next;

        for (auto &val : frontier) {
            if (!process) {
                next.push_back(val);
                continue;
            }

            sol::table input = lua.create_table();
            input["game"] = LuaGame{val.game};
            input["board"] = LuaBoard{val.board};
            input["data"] = val.data;

            sol::protected_function_result res = (*process)(input, inst->ctx);
            if (!res.valid()) {
                sol::error err = res;
                if (opts_.onError == OnError::Abort) {
                    throw std::runtime_error("plugin '" + inst->name + "' error: " + err.what());
                }
                std::println(stderr, "[{}:{}] error: {}", inst->name, index, err.what());
                if (opts_.onError == OnError::Pass) next.push_back(val);
                continue;
            }

            // Interpret the return value (see §4 shorthands).
            sol::object ret = res;
            auto applyResult = [&](sol::table t) {
                std::string action = t["action"].valid() ? t.get<std::string>("action") : "pass";
                if (action == "drop") return;
                if (action == "abort") {
                    abort = true; // drop the in-flight game and unwind the pipeline
                    return;
                }
                PluginValue out = val;
                if (t["game"].valid()) out.game = t.get<LuaGame>("game").g;
                if (t["board"].valid()) out.board = t.get<LuaBoard>("board").board;
                if (t["data"].valid()) out.data = t.get<sol::object>("data");
                if (action == "stop") stop = true;
                next.push_back(std::move(out));
            };

            if (ret == sol::lua_nil) {
                next.push_back(val);
            } else if (ret.is<bool>()) {
                if (ret.as<bool>()) next.push_back(val); // true -> pass; false -> drop
            } else if (ret.is<LuaBoard>()) {
                PluginValue out = val;
                out.board = ret.as<LuaBoard>().board;
                next.push_back(std::move(out));
            } else if (ret.is<sol::table>()) {
                sol::table t = ret.as<sol::table>();
                sol::object first = t[1];
                if (first.is<sol::table>()) {
                    for (std::size_t i = 1; i <= t.size(); ++i) applyResult(t.get<sol::table>(i)); // fan-out
                } else {
                    applyResult(t);
                }
            } else {
                next.push_back(val); // unknown -> pass
            }

            if (abort) break;
        }

        if (abort) break;
        frontier = std::move(next);
    }

    // A hard abort drops the in-flight game; a graceful stop still emits it.
    if (!abort && out_) {
        for (auto &val : frontier) out_->writeGame(val.game);
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
