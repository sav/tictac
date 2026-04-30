#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace tictac {

class GameStore;
class PositionIndex;
class SequenceIndex;

struct PositionSearchResult {
    GameId      game_id;
    HalfMoveIdx ply;
    GameHeader  header;
};

struct SequenceSearchResult {
    GameId     game_id;
    GameHeader header;
};

/// Per-record filter callback. `ply` is set for position matches, unset for opening.
/// Return true to keep, false to drop. The kept count is what `limit` caps.
using GameFilter = std::function<bool(const GameRecord&, std::optional<HalfMoveIdx>)>;

class SearchEngine {
public:
    SearchEngine(const GameStore& store, const PositionIndex& pos_idx,
                 const SequenceIndex& seq_idx);

    /// Search for games containing an exact board position (given as FEN).
    /// When `filter` is set, candidates are streamed through it and `limit`
    /// counts only accepted results.
    [[nodiscard]] std::vector<PositionSearchResult>
    search_position(std::string_view fen, std::size_t limit = 100,
                    const GameFilter& filter = {}) const;

    /// Search for games whose opening matches a sequence of SAN moves.
    /// When `filter` is set, `limit` counts only accepted results.
    [[nodiscard]] std::vector<SequenceSearchResult>
    search_opening(const std::vector<std::string>& san_moves, std::size_t limit = 100,
                   const GameFilter& filter = {}) const;

    /// Count games containing this position.
    [[nodiscard]] std::size_t position_frequency(std::string_view fen) const;

    /// Count games matching this opening sequence.
    [[nodiscard]] std::uint32_t opening_frequency(const std::vector<std::string>& san_moves) const;

private:
    /// Convert SAN move strings to CompactMove values.
    static std::vector<CompactMove> san_to_compact(const std::vector<std::string>& sans);

    const GameStore&     store_;
    const PositionIndex& pos_idx_;
    const SequenceIndex& seq_idx_;
};

} // namespace tictac
