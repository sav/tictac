#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <chess.hpp>

namespace tictac {

// One mainline move plus its PGN annotations.
struct MoveData {
    chess::Move move;
    std::string san;
    std::string comment;
    std::vector<int> nags;
};

// In-memory model of a single PGN game: ordered headers and the mainline.
// Variations (RAV) are intentionally not modelled in v1.
class Game {
public:
    std::string startFen = std::string(chess::constants::STARTPOS);
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<MoveData> moves;

    const std::string *findHeader(const std::string &key) const;
    void setHeader(const std::string &key, const std::string &value);
    bool removeHeader(const std::string &key);

    std::string result() const;
    std::size_t moveCount() const { return moves.size(); }

    chess::Board startBoard() const;
    // Board after `ply` half-moves; ply < 0 means the final position.
    chess::Board boardAt(int ply) const;

    std::string pgn() const;
    std::shared_ptr<Game> clone() const;
};

// Parse every game in `stream` into the model.
std::vector<std::shared_ptr<Game>> parseGames(std::istream &stream);

} // namespace tictac
