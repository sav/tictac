#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chess.hpp"

namespace tictac {

using ZobristKey  = std::uint64_t;
using GameId      = std::uint32_t;
using HalfMoveIdx = std::uint16_t;
using CompactMove = std::uint16_t;

struct __attribute__((packed)) PositionOccurrence {
    GameId      game_id;
    HalfMoveIdx ply;
};
static_assert(sizeof(PositionOccurrence) == 6);

struct GameHeader {
    std::string white;
    std::string black;
    std::string event;
    std::string date;
    chess::GameResult result{};
    std::uint16_t white_elo = 0;
    std::uint16_t black_elo = 0;
};

struct GameRecord {
    GameId                    id = 0;
    GameHeader                header;
    std::vector<CompactMove>  moves;
};

} // namespace tictac
