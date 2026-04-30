#include "search/search_engine.hpp"

#include "chess.hpp"
#include "storage/game_store.hpp"
#include "storage/position_index.hpp"
#include "storage/sequence_index.hpp"

#include <algorithm>

namespace tictac {

SearchEngine::SearchEngine(const GameStore& store, const PositionIndex& pos_idx,
                           const SequenceIndex& seq_idx)
    : store_(store), pos_idx_(pos_idx), seq_idx_(seq_idx)
{}

std::vector<PositionSearchResult>
SearchEngine::search_position(std::string_view fen, std::size_t limit,
                              const GameFilter& filter) const {
    chess::Board board{std::string(fen)};
    auto key = board.hash();

    // Without a filter we can ask the index to truncate. With a filter we
    // need every candidate so `limit` can count accepted results.
    auto occurrences = pos_idx_.lookup(key, filter ? 0 : limit);

    std::vector<PositionSearchResult> results;
    results.reserve(std::min(occurrences.size(), limit));

    for (const auto& occ : occurrences) {
        if (results.size() >= limit) break;
        try {
            auto game = store_.load(occ.game_id);

            // Verify position by replaying (guard against Zobrist collisions)
            chess::Board replay;
            for (HalfMoveIdx i = 0; i < occ.ply && i < game.moves.size(); ++i) {
                chess::Move m(game.moves[i]);
                replay.makeMove(m);
            }
            if (replay.hash() != key) continue; // Zobrist collision

            if (filter && !filter(game, occ.ply)) continue;

            results.push_back({occ.game_id, occ.ply, std::move(game.header)});
        } catch (...) {
            // Skip games that fail to load
        }
    }

    return results;
}

std::vector<SequenceSearchResult>
SearchEngine::search_opening(const std::vector<std::string>& san_moves,
                             std::size_t limit, const GameFilter& filter) const {
    auto compact = san_to_compact(san_moves);
    auto game_ids = seq_idx_.search_prefix(compact);

    std::vector<SequenceSearchResult> results;
    results.reserve(std::min<std::size_t>(game_ids.size(), limit));

    for (auto id : game_ids) {
        if (results.size() >= limit) break;
        try {
            auto game = store_.load(id);
            if (filter && !filter(game, std::nullopt)) continue;
            results.push_back({id, std::move(game.header)});
        } catch (...) {
            // Skip
        }
    }

    return results;
}

std::size_t SearchEngine::position_frequency(std::string_view fen) const {
    chess::Board board{std::string(fen)};
    return pos_idx_.count(board.hash());
}

std::uint32_t SearchEngine::opening_frequency(const std::vector<std::string>& san_moves) const {
    auto compact = san_to_compact(san_moves);
    return seq_idx_.count(compact);
}

std::vector<CompactMove> SearchEngine::san_to_compact(const std::vector<std::string>& sans) {
    chess::Board board;
    std::vector<CompactMove> result;
    result.reserve(sans.size());
    for (const auto& san : sans) {
        auto m = chess::uci::parseSan(board, san);
        result.push_back(m.move());
        board.makeMove(m);
    }
    return result;
}

} // namespace tictac
