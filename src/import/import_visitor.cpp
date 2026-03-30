#include "import/import_visitor.hpp"

#include <charconv>
#include <string>

namespace tictac {

ImportVisitor::ImportVisitor(GameCallback callback)
    : callback_(std::move(callback))
{}

void ImportVisitor::startPgn() {
    current_ = GameRecord{};
    current_.id = next_id_;
    board_ = chess::Board();
    has_error_ = false;
}

void ImportVisitor::header(std::string_view key, std::string_view value) {
    if (key == "White") {
        current_.header.white = std::string(value);
    } else if (key == "Black") {
        current_.header.black = std::string(value);
    } else if (key == "Event") {
        current_.header.event = std::string(value);
    } else if (key == "Date") {
        current_.header.date = std::string(value);
    } else if (key == "Result") {
        if (value == "1-0")
            current_.header.result = chess::GameResult::WIN;
        else if (value == "0-1")
            current_.header.result = chess::GameResult::LOSE;
        else if (value == "1/2-1/2")
            current_.header.result = chess::GameResult::DRAW;
        else
            current_.header.result = chess::GameResult::NONE;
    } else if (key == "WhiteElo") {
        std::uint16_t elo = 0;
        std::from_chars(value.data(), value.data() + value.size(), elo);
        current_.header.white_elo = elo;
    } else if (key == "BlackElo") {
        std::uint16_t elo = 0;
        std::from_chars(value.data(), value.data() + value.size(), elo);
        current_.header.black_elo = elo;
    }
}

void ImportVisitor::startMoves() {
    current_.moves.clear();
    current_.moves.reserve(80); // typical game ~40 moves = 80 half-moves
}

void ImportVisitor::move(std::string_view san, [[maybe_unused]] std::string_view comment) {
    if (has_error_) return;

    try {
        chess::Move m = chess::uci::parseSan(board_, san);
        current_.moves.push_back(m.move());
        board_.makeMove(m);
    } catch (...) {
        has_error_ = true;
    }
}

void ImportVisitor::endPgn() {
    if (has_error_) {
        ++parse_errors_;
        return;
    }

    callback_(std::move(current_));
    ++next_id_;
}

} // namespace tictac
