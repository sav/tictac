#include "runtime.hpp"

#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "image.hpp"

namespace tictac {

// --- Writer -----------------------------------------------------------------

Writer::Writer() : os_(&std::cout) {}

Writer::Writer(const std::string &path, const std::string &mode) {
    auto flags = std::ios::out;
    if (mode == "a") flags |= std::ios::app;
    else flags |= std::ios::trunc;
    file_.open(path, flags);
    if (!file_) throw std::runtime_error("cannot open writer: " + path);
    os_ = &file_;
}

void Writer::write(const std::string &text) {
    *os_ << text << std::flush;
}

void Writer::writeGame(const std::shared_ptr<Game> &game) {
    *os_ << game->pgn() << "\n" << std::flush;
}

// --- Helpers ----------------------------------------------------------------

namespace {

std::optional<chess::Move> parseMove(const chess::Board &board, const std::string &text) {
    try {
        return chess::uci::parseSan(board, text);
    } catch (...) {
    }
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto &m : moves) {
        if (chess::uci::moveToUci(m) == text) return m;
    }
    return std::nullopt;
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

LuaMove makeLuaMove(const chess::Board &before, chess::Move mv, std::shared_ptr<Game> game = nullptr,
                    int ply = -1) {
    return LuaMove{mv, before, std::move(game), ply};
}

} // namespace

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

namespace {

AnalysisLimits readLimits(const sol::table &t) {
    AnalysisLimits lim;
    if (t["depth"].valid()) lim.depth = t.get<int>("depth");
    if (t["movetime"].valid()) lim.movetime = t.get<int>("movetime");
    if (t["nodes"].valid()) lim.nodes = t.get<long long>("nodes");
    if (t["multipv"].valid()) lim.multipv = t.get<int>("multipv");
    return lim;
}

ImageOptions readImageOptions(sol::optional<sol::table> opts) {
    ImageOptions io;
    if (!opts) return io;
    sol::table t = *opts;
    if (t["size"].valid()) io.size = t.get<int>("size");
    if (t["format"].valid()) io.format = t.get<std::string>("format");
    if (t["flip"].valid()) io.flip = t.get<bool>("flip");
    if (t["coordinates"].valid()) io.coordinates = t.get<bool>("coordinates");
    if (t["theme"].valid()) io.theme = t.get<std::string>("theme");
    if (t["lastMove"].valid()) {
        sol::object lm = t["lastMove"];
        if (lm.is<LuaMove>()) {
            const auto &m = lm.as<LuaMove>();
            io.lastMove = {squareStr(m.move.from()), squareStr(m.move.to())};
        }
    }
    if (t["highlight"].valid()) {
        sol::table h = t["highlight"];
        for (std::size_t i = 1; i <= h.size(); ++i) io.highlight.push_back(h.get<std::string>(i));
    }
    if (t["arrows"].valid()) {
        sol::table arr = t["arrows"];
        for (std::size_t i = 1; i <= arr.size(); ++i) {
            sol::table a = arr[i];
            io.arrows.emplace_back(a.get<std::string>(1), a.get<std::string>(2));
        }
    }
    return io;
}

} // namespace

void Runtime::registerTypes() {
    sol::state_view lua = lua_;

    // Move -------------------------------------------------------------------
    lua.new_usertype<LuaMove>(
        "Move", sol::no_constructor, //
        "san", [](LuaMove &m) { return chess::uci::moveToSan(m.before, m.move); }, //
        "uci", [](LuaMove &m) { return chess::uci::moveToUci(m.move); },           //
        "from", [](LuaMove &m) { return squareStr(m.move.from()); },               //
        "to", [](LuaMove &m) { return squareStr(m.move.to()); },                   //
        "piece",
        [](LuaMove &m) {
            return static_cast<std::string>(m.before.at<chess::PieceType>(m.move.from()));
        },
        "isCapture", [](LuaMove &m) { return m.before.isCapture(m.move); }, //
        "isCheck",
        [](LuaMove &m) {
            chess::Board b = m.before;
            b.makeMove(m.move);
            return b.inCheck();
        },
        "isPromotion", [](LuaMove &m) { return m.move.typeOf() == chess::Move::PROMOTION; }, //
        "promotion",
        [](LuaMove &m, sol::this_state ts) -> sol::object {
            if (m.move.typeOf() != chess::Move::PROMOTION) return sol::lua_nil;
            return sol::make_object(ts, static_cast<std::string>(m.move.promotionType()));
        },
        "comment",
        [](LuaMove &m, sol::this_state ts) -> sol::object {
            if (m.game && m.ply >= 0) {
                const auto &c = m.game->moves[static_cast<std::size_t>(m.ply)].comment;
                if (!c.empty()) return sol::make_object(ts, c);
            }
            return sol::lua_nil;
        },
        "setComment",
        [](LuaMove &m, const std::string &text) {
            if (m.game && m.ply >= 0) m.game->moves[static_cast<std::size_t>(m.ply)].comment = text;
        },
        "nags",
        [](LuaMove &m, sol::this_state ts) {
            sol::state_view l(ts);
            sol::table t = l.create_table();
            if (m.game && m.ply >= 0) {
                const auto &n = m.game->moves[static_cast<std::size_t>(m.ply)].nags;
                for (std::size_t i = 0; i < n.size(); ++i) t[i + 1] = n[i];
            }
            return t;
        },
        "addNag",
        [](LuaMove &m, int code) {
            if (m.game && m.ply >= 0) m.game->moves[static_cast<std::size_t>(m.ply)].nags.push_back(code);
        });

    // Board ------------------------------------------------------------------
    lua.new_usertype<LuaBoard>(
        "Board", sol::no_constructor, //
        "fen", [](LuaBoard &b) { return b.board.getFen(); }, //
        "setFen", [](LuaBoard &b, const std::string &fen) { b.board.setFen(fen); }, //
        "sideToMove",
        [](LuaBoard &b) {
            return b.board.sideToMove() == chess::Color::WHITE ? std::string("white") : std::string("black");
        },
        "fullmoveNumber", [](LuaBoard &b) { return b.board.fullMoveNumber(); }, //
        "halfmoveClock", [](LuaBoard &b) { return b.board.halfMoveClock(); },   //
        "legalMoves",
        [](LuaBoard &b) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, b.board);
            std::vector<LuaMove> out;
            for (const auto &m : moves) out.push_back(makeLuaMove(b.board, m));
            return sol::as_table(out);
        },
        "isLegal",
        [](LuaBoard &b, const std::string &mv) { return parseMove(b.board, mv).has_value(); }, //
        "makeMove",
        [](LuaBoard &b, const std::string &mv) {
            auto parsed = parseMove(b.board, mv);
            if (!parsed) throw std::runtime_error("illegal move: " + mv);
            LuaBoard nb{b.board};
            nb.board.makeMove(*parsed);
            return nb;
        },
        "piece",
        [](LuaBoard &b, const std::string &sq, sol::this_state ts) { return pieceAt(ts, b.board, sq); }, //
        "pieces",
        [](LuaBoard &b, sol::optional<sol::table> filter, sol::this_state ts) {
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
        "isCheck", [](LuaBoard &b) { return b.board.inCheck(); }, //
        "isCheckmate",
        [](LuaBoard &b) {
            auto [reason, result] = b.board.isGameOver();
            return reason == chess::GameResultReason::CHECKMATE;
        },
        "isStalemate",
        [](LuaBoard &b) {
            auto [reason, result] = b.board.isGameOver();
            return reason == chess::GameResultReason::STALEMATE;
        },
        "isInsufficientMaterial", [](LuaBoard &b) { return b.board.isInsufficientMaterial(); }, //
        "isRepetition",
        [](LuaBoard &b, sol::optional<int> count) { return b.board.isRepetition(count.value_or(2)); }, //
        "phase",
        [](LuaBoard &b) {
            int total = 0;
            for (int i = 0; i < 64; ++i) {
                chess::Piece p = b.board.at(chess::Square(i));
                if (p == chess::Piece::NONE) continue;
                if (p.type() == chess::PieceType::PAWN || p.type() == chess::PieceType::KING) continue;
                total += pieceValue(p.type());
            }
            if (b.board.fullMoveNumber() <= 10) return std::string("opening");
            if (total <= 1300) return std::string("endgame");
            return std::string("middlegame");
        },
        "material",
        [](LuaBoard &b, sol::this_state ts) {
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
        },
        "image",
        [](LuaBoard &b, const std::string &path, sol::optional<sol::table> opts) {
            return renderImage(b.board.getFen(), path, readImageOptions(opts));
        });

    // Game -------------------------------------------------------------------
    lua.new_usertype<LuaGame>(
        "Game", sol::no_constructor, //
        "header",
        [](LuaGame &g, const std::string &key, sol::this_state ts) -> sol::object {
            const std::string *v = g.g->findHeader(key);
            if (!v) return sol::lua_nil;
            return sol::make_object(ts, *v);
        },
        "headers",
        [](LuaGame &g, sol::this_state ts) {
            sol::state_view l(ts);
            sol::table t = l.create_table();
            for (const auto &[k, v] : g.g->headers) t[k] = v;
            return t;
        },
        "setHeader",
        [](LuaGame &g, const std::string &k, const std::string &v) { g.g->setHeader(k, v); }, //
        "removeHeader", [](LuaGame &g, const std::string &k) { return g.g->removeHeader(k); }, //
        "result", [](LuaGame &g) { return g.g->result(); },                                    //
        "moveCount", [](LuaGame &g) { return g.g->moveCount(); },                              //
        "moves",
        [](LuaGame &g) {
            std::vector<LuaMove> out;
            chess::Board board = g.g->startBoard();
            for (std::size_t i = 0; i < g.g->moves.size(); ++i) {
                out.push_back(makeLuaMove(board, g.g->moves[i].move, g.g, static_cast<int>(i)));
                board.makeMove(g.g->moves[i].move);
            }
            return sol::as_table(out);
        },
        "startBoard", [](LuaGame &g) { return LuaBoard{g.g->startBoard()}; }, //
        "board",
        [](LuaGame &g, sol::optional<int> ply) {
            return LuaBoard{g.g->boardAt(ply.value_or(-1))};
        },
        "positions",
        [](LuaGame &g, sol::this_state ts) {
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
                    node["move"] = makeLuaMove(before, game->moves[i].move, game, static_cast<int>(i));
                    node["board_before"] = LuaBoard{before};
                    node["board_after"] = LuaBoard{after};
                    node["board"] = LuaBoard{after};
                    ++(*idx);
                    return node;
                }));
        },
        "pgn", [](LuaGame &g) { return g.g->pgn(); }, //
        "clone", [](LuaGame &g) { return LuaGame{g.g->clone()}; });

    // Writer -----------------------------------------------------------------
    lua.new_usertype<Writer>(
        "Writer", sol::no_constructor, //
        "write", &Writer::write,       //
        "writef",
        [](Writer &w, sol::this_state ts, const std::string &fmt, sol::variadic_args va) {
            sol::state_view l(ts);
            sol::protected_function format = l["string"]["format"];
            sol::protected_function_result r = format(fmt, va);
            if (!r.valid()) {
                sol::error e = r;
                throw std::runtime_error(e.what());
            }
            w.write(r.get<std::string>());
        },
        "writeGame", [](Writer &w, LuaGame &g) { w.writeGame(g.g); }, //
        "flush", &Writer::flush,                                      //
        "close", &Writer::close);

    // Engine -----------------------------------------------------------------
    lua.new_usertype<Engine>(
        "Engine", sol::no_constructor, //
        "setOption",
        [](Engine &e, const std::string &name, sol::object value) {
            e.setOption(name, value.as<std::string>());
        },
        "analyse",
        [](Engine &e, LuaBoard &b, sol::table limits, sol::this_state ts) {
            sol::state_view l(ts);
            return analysisToTable(l, e.analyse(b.board.getFen(), readLimits(limits)));
        },
        "bestmove",
        [](Engine &e, LuaBoard &b, sol::table limits) {
            return e.analyse(b.board.getFen(), readLimits(limits)).bestmove;
        },
        "cp",
        [](Engine &e, LuaBoard &b, sol::table limits) -> double {
            Analysis a = e.analyse(b.board.getFen(), readLimits(limits));
            double cp;
            if (a.mate) cp = (*a.mate >= 0 ? 1.0 : -1.0) * 100000.0;
            else cp = a.score.value_or(0.0);
            if (b.board.sideToMove() == chess::Color::BLACK) cp = -cp;
            return cp;
        });

    // Args -------------------------------------------------------------------
    lua.new_usertype<Args>(
        "Args", sol::no_constructor, //
        "get",
        [](Args &a, const std::string &key, sol::variadic_args va, sol::this_state ts) -> sol::object {
            auto it = a.values.find(key);
            if (it != a.values.end()) return sol::make_object(ts, it->second);
            if (va.size() > 0) return va[0].get<sol::object>();
            return sol::lua_nil;
        },
        "number",
        [](Args &a, const std::string &key, sol::optional<double> def) -> double {
            auto it = a.values.find(key);
            if (it != a.values.end()) {
                try {
                    return std::stod(it->second);
                } catch (...) {
                }
            }
            return def.value_or(0.0);
        },
        "bool",
        [](Args &a, const std::string &key, sol::optional<bool> def) -> bool {
            auto it = a.values.find(key);
            if (it != a.values.end()) {
                const std::string &v = it->second;
                return v == "true" || v == "1" || v == "yes" || v == "on";
            }
            return def.value_or(false);
        },
        "require",
        [](Args &a, const std::string &key) -> std::string {
            auto it = a.values.find(key);
            if (it == a.values.end()) throw std::runtime_error("missing required argument: " + key);
            return it->second;
        },
        "list",
        [](Args &a, const std::string &key, sol::this_state ts) {
            sol::state_view l(ts);
            sol::table out = l.create_table();
            auto it = a.values.find(key);
            if (it != a.values.end()) {
                std::stringstream ss(it->second);
                std::string item;
                int n = 0;
                while (std::getline(ss, item, ',')) out[++n] = item;
            }
            return out;
        });
}

sol::table Runtime::buildCtx(PluginInstance &inst) {
    sol::state_view lua = lua_;
    sol::table ctx = lua.create_table();

    ctx["args"] = inst.args;
    ctx["shared"] = shared_;
    ctx["state"] = lua.create_table();
    ctx["out"] = out_;

    PluginInstance *self = &inst;

    ctx["engine"] = [this, self](const std::string &path, sol::optional<sol::table> opts) {
        auto it = self->engines.find(path);
        if (it != self->engines.end()) return it->second;
        std::map<std::string, std::string> options;
        if (opts && (*opts)["options"].valid()) {
            sol::table o = (*opts)["options"];
            for (auto &kv : o) {
                std::string key = kv.first.as<std::string>();
                sol::object val = kv.second;
                options[key] = val.is<std::string>() ? val.as<std::string>()
                                                      : std::to_string(val.as<double>());
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

    ctx["writer"] = [self](const std::string &path) {
        auto it = self->writers.find(path);
        if (it != self->writers.end()) return it->second;
        auto w = std::make_shared<Writer>(path, "w");
        self->writers[path] = w;
        self->managed.push_back(w);
        return w;
    };

    ctx["emit"] = [this](LuaGame &g, sol::optional<std::shared_ptr<Writer>> writer) {
        if (writer && *writer) (*writer)->writeGame(g.g);
        else if (out_) out_->writeGame(g.g);
    };

    ctx["image"] = [](LuaBoard &b, const std::string &path, sol::optional<sol::table> opts) {
        return renderImage(b.board.getFen(), path, readImageOptions(opts));
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

} // namespace tictac
