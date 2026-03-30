#pragma once

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/types.hpp"

namespace tictac {

/// In-memory trie for opening move sequences (first N plies).
///
/// Each node represents a position after a specific move sequence.
/// Stores which games pass through each node.
/// Persisted to disk as flat arrays.
class SequenceIndex {
public:
    explicit SequenceIndex(const std::filesystem::path& db_path, unsigned max_depth = 30);

    /// Insert a game's move sequence into the trie.
    void insert(GameId game_id, const std::vector<CompactMove>& moves);

    /// Find games whose opening matches this exact move prefix.
    [[nodiscard]] std::vector<GameId> search_prefix(const std::vector<CompactMove>& moves) const;

    /// Count games passing through this move sequence.
    [[nodiscard]] std::uint32_t count(const std::vector<CompactMove>& moves) const;

    /// Persist the trie to disk.
    void save() const;

    /// Load the trie from disk.
    void load();

    [[nodiscard]] unsigned max_depth() const { return max_depth_; }

private:
    struct TrieNode {
        std::unordered_map<CompactMove, std::uint32_t> children; // move -> child node index
        std::vector<GameId> game_ids;
    };

    /// Walk the trie for a move sequence, returning the final node index.
    /// Returns -1 if the sequence doesn't exist in the trie.
    [[nodiscard]] std::int64_t walk(const std::vector<CompactMove>& moves) const;

    /// Walk the trie, creating nodes as needed. Returns final node index.
    std::uint32_t walk_or_create(const std::vector<CompactMove>& moves);

    std::filesystem::path db_path_;
    unsigned max_depth_;
    std::vector<TrieNode> nodes_; // nodes_[0] = root
    mutable std::mutex mutex_;
};

} // namespace tictac
