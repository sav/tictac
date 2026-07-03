// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Lua plugin runtime and pipeline: sol2 bindings and pipeline execution.

#include "runtime.hpp"

#include <format>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace tictac {

namespace {

std::optional<chess::Move> parseMove(const chess::Board &board, std::string_view move) {
    // try to parse `move` as SAN first; fall back to matching an UCI format if it fails.
    try {
        return chess::uci::parseSan(board, move);
    } catch (const chess::uci::SanParseError &) {
    } catch (const chess::uci::AmbiguousMoveError &) {}
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto &m : moves) {
        if (chess::uci::moveToUci(m) == move) return m;
    }
    return std::nullopt;
}

LuaMove makeLuaMove(chess::Move mv, const chess::Board &before, std::shared_ptr<Game> game = nullptr, int ply = -1) {
    return LuaMove{mv, before, std::move(game), ply};
}

std::string squareStr(chess::Square sq) { return static_cast<std::string>(sq); }

sol::object pieceAt(sol::this_state ts, const chess::Board &board, const std::string &sq) {
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

sol::table analysisToTable(sol::state_view lua, const Analysis &a) {
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

AnalysisLimits readLimits(const sol::table &t) {
    AnalysisLimits lim;
    if (t["depth"].valid()) lim.depth = t.get<int>("depth");
    if (t["movetime"].valid()) lim.movetime = t.get<int>("movetime");
    if (t["nodes"].valid()) lim.nodes = t.get<long long>("nodes");
    if (t["multipv"].valid()) lim.multipv = t.get<int>("multipv");
    return lim;
}

} // namespace

Runtime::Runtime(RunOptions opts) : opts_(std::move(opts)) {
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::os, sol::lib::io,
                        sol::lib::package);

    shared_ = lua_.create_table();

    if (!opts_.noOutput) {
        out_ = opts_.output == "-" ? std::make_shared<Writer>() : std::make_shared<Writer>(opts_.output, "w");
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
                std::cerr << "error: init of '" << inst->name << "': " << err.what() << "\n";
                return 1; // init failure means the plugin can't run; always abort, regardless of --on-error
            }
        }
    }

    std::size_t index = 0;
    bool aborted = false;
    for (const auto &file : opts_.files) {
        if (aborted) break;
        std::ifstream in(file);
        if (!in) {
            std::cerr << "error: cannot open file: " << file << "\n";
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
                std::cerr << "error: finish of '" << inst->name << "': " << err.what() << "\n";
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
                const auto &c = m.game->moves[static_cast<std::size_t>(m.ply)].comment;
                if (!c.empty()) return sol::make_object(ts, c);
            }
            return sol::lua_nil;
        },
        "setComment", [](LuaMove &m, const std::string &text) {
            if (m.game && m.ply >= 0) m.game->moves[static_cast<std::size_t>(m.ply)].comment = text;
        },
        "nags", [](LuaMove &m, sol::this_state ts) {
            sol::state_view l(ts);
            sol::table t = l.create_table();
            if (m.game && m.ply >= 0) {
                const auto &n = m.game->moves[static_cast<std::size_t>(m.ply)].nags;
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
        "setFen", [](LuaBoard &b, const std::string &fen) { b.board.setFen(fen); },
        "sideToMove", [](LuaBoard &b) {
            return b.board.sideToMove() == chess::Color::WHITE ? std::string("white") : std::string("black");
        },
        "fullmoveNumber", [](LuaBoard &b) { return b.board.fullMoveNumber(); },
        "halfmoveClock", [](LuaBoard &b) { return b.board.halfMoveClock(); },
        "legalMoves", [](LuaBoard &b) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, b.board);
            std::vector<LuaMove> out;
            for (const auto &m : moves) out.push_back(makeLuaMove(m, b.board));
            return sol::as_table(out);
        },
        "isLegal", [](LuaBoard &b, const std::string &mv) { return parseMove(b.board, mv).has_value(); },
        "makeMove", [](LuaBoard &b, const std::string &mv) {
            auto parsed = parseMove(b.board, mv);
            if (!parsed) throw std::runtime_error("illegal move: " + mv);
            LuaBoard nb{b.board};
            nb.board.makeMove(*parsed);
            return nb;
        },
        "piece", [](LuaBoard &b, const std::string &sq, sol::this_state ts) { return pieceAt(ts, b.board, sq); },
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
            auto [reason, result] = b.board.isGameOver();
            return reason == chess::GameResultReason::CHECKMATE;
        },
        "isStalemate", [](LuaBoard &b) {
            auto [reason, result] = b.board.isGameOver();
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
        "header", [](LuaGame &g, const std::string &key, sol::this_state ts) -> sol::object {
            const std::string *v = g.g->findHeader(key);
            if (!v) return sol::lua_nil;
            return sol::make_object(ts, *v);
        },
        "headers", [](LuaGame &g, sol::this_state ts) {
            sol::state_view l(ts);
            sol::table t = l.create_table();
            for (const auto &[k, v] : g.g->headers) t[k] = v;
            return t;
        },
        "setHeader", [](LuaGame &g, const std::string &k, const std::string &v) { g.g->setHeader(k, v); },
        "removeHeader", [](LuaGame &g, const std::string &k) { return g.g->removeHeader(k); },
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
        "writef", [](Writer &w, sol::this_state ts, const std::string &fmt, sol::variadic_args va) {
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
        "setOption", [](Engine &e, const std::string &name, sol::object value) {
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
            double cp;
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
        "get", [](Args &a, const std::string &key, sol::variadic_args va, sol::this_state ts) -> sol::object {
            const std::string *found = nullptr;
            for (const auto &[k, v] : a.values)
                if (k == key) found = &v;
            if (found) return sol::make_object(ts, *found);
            if (va.size() > 0) return va[0].get<sol::object>();
            return sol::lua_nil;
        },
        "number", [](Args &a, const std::string &key, sol::optional<double> def) -> double {
            const std::string *found = nullptr;
            for (const auto &[k, v] : a.values)
                if (k == key) found = &v;
            if (found) {
                double value = 0.0;
                auto [ptr, ec] = std::from_chars(found->data(), found->data() + found->size(), value);
                if (ec == std::errc{}) return value;
            }
            return def.value_or(0.0);
        },
        "bool", [](Args &a, const std::string &key, sol::optional<bool> def) -> bool {
            const std::string *found = nullptr;
            for (const auto &[k, v] : a.values)
                if (k == key) found = &v;
            if (found) {
                const std::string &v = *found;
                return v == "true" || v == "1" || v == "yes" || v == "on";
            }
            return def.value_or(false);
        },
        "require", [](Args &a, const std::string &key) -> std::string {
            const std::string *found = nullptr;
            for (const auto &[k, v] : a.values)
                if (k == key) found = &v;
            if (!found) throw std::runtime_error("missing required argument: " + key);
            return *found;
        },
        "list", [](Args &a, const std::string &key, sol::this_state ts) -> sol::object {
            bool found = false;
            sol::state_view l(ts);
            sol::table out = l.create_table();
            int n = 0;
            for (const auto &[k, v] : a.values) {
                if (k != key) continue;
                found = true;
                std::stringstream ss(v);
                std::string item;
                while (std::getline(ss, item, ',')) out[++n] = item;
            }
            if (!found) return sol::lua_nil;
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
    for (const auto &spec : opts_.plugins) {
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
    sol::state_view lua = lua_;
    sol::table ctx = lua.create_table();

    ctx["args"] = inst.args;
    ctx["shared"] = shared_;
    ctx["state"] = lua.create_table();
    ctx["out"] = out_;

    PluginInstance *self = &inst;

    ctx["engine"] = [self](const std::string &path, sol::optional<sol::table> opts) {
        auto it = self->engines.find(path);
        if (it != self->engines.end()) return it->second;
        std::map<std::string, std::string> options;
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

    ctx["open"] = [self](const std::string &path, sol::optional<std::string> mode) {
        auto w = std::make_shared<Writer>(path, mode.value_or("w"));
        self->managed.push_back(w);
        return w;
    };

    const std::string name = inst.name;
    sol::table log = lua.create_table();
    const auto logger = [this, name](const char *level) {
        return [this, name, level](sol::this_state ts, const std::string &fmt, sol::variadic_args va) {
            sol::state_view l(ts);
            sol::protected_function format = l["string"]["format"];
            sol::protected_function_result r = format(fmt, va);
            std::string msg = r.valid() ? r.get<std::string>() : fmt;
            std::cerr << "[" << name << ":" << current_index_ << "] " << level << ": " << msg << "\n";
        };
    };
    log["info"] = logger("info");
    log["warn"] = logger("warn");
    log["error"] = logger("error");
    log["debug"] = logger("debug");
    ctx["log"] = log;

    return ctx;
}

bool Runtime::processGame(const std::shared_ptr<Game> &game, std::size_t index) {
    sol::state_view lua = lua_;
    bool abort = false;

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
                std::cerr << "[" << inst->name << ":" << index << "] error: " << err.what() << "\n";
                if (opts_.onError == OnError::Pass) next.push_back(val);
                continue;
            }

            // Interpret the return value (see §4 shorthands).
            sol::object ret = res;
            auto applyResult = [&](sol::table t) {
                std::string action = t["action"].valid() ? t.get<std::string>("action") : "pass";
                if (action == "drop") return;
                PluginValue out = val;
                if (t["game"].valid()) out.game = t.get<LuaGame>("game").g;
                if (t["board"].valid()) out.board = t.get<LuaBoard>("board").board;
                if (t["data"].valid()) out.data = t.get<sol::object>("data");
                if (action == "abort") abort = true;
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
        }

        frontier = std::move(next);
    }

    if (out_) {
        for (auto &val : frontier) out_->writeGame(val.game);
    }
    return abort;
}

PluginSpec parsePluginSpec(const std::string &spec) {
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
