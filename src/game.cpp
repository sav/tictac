#include "game.hpp"

#include <cstdio>
#include <print>
#include <sstream>

namespace tictac {

const std::string *Game::findHeader(const std::string &key) const {
    for (const auto &[k, v] : headers) {
        if (k == key) return &v;
    }
    return nullptr;
}

void Game::setHeader(const std::string &key, const std::string &value) {
    for (auto &[k, v] : headers) {
        if (k == key) {
            v = value;
            return;
        }
    }
    headers.emplace_back(key, value);
}

bool Game::removeHeader(const std::string &key) {
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (it->first == key) {
            headers.erase(it);
            return true;
        }
    }
    return false;
}

std::string Game::result() const {
    if (const auto *r = findHeader("Result")) return *r;
    return "*";
}

chess::Board Game::startBoard() const { return chess::Board(startFen); }

chess::Board Game::boardAt(int ply) const {
    chess::Board board(startFen);
    const std::size_t limit =
        ply < 0 ? moves.size() : std::min<std::size_t>(static_cast<std::size_t>(ply), moves.size());
    for (std::size_t i = 0; i < limit; ++i) {
        board.makeMove(moves[i].move);
    }
    return board;
}

std::string Game::pgn() const {
    std::ostringstream out;
    for (const auto &[k, v] : headers) {
        out << '[' << k << " \"" << v << "\"]\n";
    }
    out << '\n';

    chess::Board board(startFen);
    std::string line;
    const auto flush_token = [&](const std::string &token) {
        if (line.empty()) {
            line = token;
        } else if (line.size() + 1 + token.size() > 80) {
            out << line << '\n';
            line = token;
        } else {
            line += ' ';
            line += token;
        }
    };

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto &md = moves[i];
        const bool white = board.sideToMove() == chess::Color::WHITE;
        if (white) {
            flush_token(std::to_string(board.fullMoveNumber()) + ".");
        } else if (i == 0) {
            flush_token(std::to_string(board.fullMoveNumber()) + "...");
        }
        flush_token(md.san);
        for (int nag : md.nags) {
            flush_token("$" + std::to_string(nag));
        }
        if (!md.comment.empty()) {
            flush_token("{" + md.comment + "}");
        }
        board.makeMove(md.move);
    }

    flush_token(result());
    out << line << "\n";
    return out.str();
}

std::shared_ptr<Game> Game::clone() const { return std::make_shared<Game>(*this); }

namespace {

class Builder : public chess::pgn::Visitor {
public:
    explicit Builder(std::vector<std::shared_ptr<Game>> &out) : out_(out) {}

    void startPgn() override {
        current_ = std::make_shared<Game>();
        board_.setFen(chess::constants::STARTPOS);
        has_fen_ = false;
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            current_->startFen = std::string(value);
            board_.setFen(value);
            has_fen_ = true;
        }
        current_->setHeader(std::string(key), std::string(value));
    }

    void startMoves() override {}

    void move(std::string_view move, std::string_view comment) override {
        auto mv = chess::uci::parseSan(board_, move);
        MoveData md;
        md.move = mv;
        md.san = std::string(move);
        md.comment = std::string(comment);
        current_->moves.push_back(std::move(md));
        board_.makeMove(mv);
    }

    void endPgn() override {
        if (!has_fen_) current_->startFen = std::string(chess::constants::STARTPOS);
        out_.push_back(current_);
    }

private:
    std::vector<std::shared_ptr<Game>> &out_;
    std::shared_ptr<Game> current_;
    chess::Board board_;
    bool has_fen_ = false;
};

} // namespace

std::vector<std::shared_ptr<Game>> parseGames(std::istream &stream) {
    std::vector<std::shared_ptr<Game>> games;
    Builder builder(games);
    chess::pgn::StreamParser parser(stream);
    if (auto err = parser.readGames(builder); err.hasError()) {
        std::println(stderr, "warning: PGN parse error: {}", err.message());
    }
    return games;
}

} // namespace tictac
