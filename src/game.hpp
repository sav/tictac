// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// PGN game model: moves, headers, and the Game type.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <chess.hpp>

namespace tictac {

// A single move of the mainline, plus the PGN annotations attached to it.
struct MoveData {
    chess::Move move;
    std::string san;
    std::string comment;
    std::vector<int> nags;
};

// In-memory model of a single PGN game: ordered headers and the mainline.
// Variations (RAV) are not modelled: the parser skips them, so a read-write
// round-trip drops them.
class Game {
public:
    std::string startFen = chess::constants::STARTPOS;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<MoveData> moves;

    // The view is invalidated by any later setHeader/removeHeader call.
    [[nodiscard]] std::optional<std::string_view> findHeader(std::string const &key) const;
    // Overwrites the value in place if `key` already exists; appends otherwise.
    void setHeader(std::string const &key, std::string const &value);
    bool removeHeader(std::string const &key);

    [[nodiscard]] std::string result() const;
    [[nodiscard]] std::size_t moveCount() const { return moves.size(); }

    [[nodiscard]] chess::Board startBoard() const;
    // Board after `ply` half-moves; ply < 0 means the final position.
    [[nodiscard]] chess::Board boardAt(int ply) const;

    [[nodiscard]] std::string pgn() const;
    [[nodiscard]] std::shared_ptr<Game> clone() const;
};

// Parse every game in `stream` into the model.
[[nodiscard]] std::vector<std::shared_ptr<Game>> parseGames(std::istream &stream);

} // namespace tictac
