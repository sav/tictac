#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include "core/types.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace tictac {

/// SQLite-backed trie for opening move sequences (first N plies).
///
/// Each node represents a position after a specific move sequence.
/// Stores which games pass through each node. Persisted as a single
/// SQLite database file inside the configured db path.
class SequenceIndex {
public:
    explicit SequenceIndex(const std::filesystem::path& db_path, unsigned max_depth = 30);
    ~SequenceIndex();

    SequenceIndex(const SequenceIndex&) = delete;
    SequenceIndex& operator=(const SequenceIndex&) = delete;

    /// Insert a game's move sequence into the trie.
    void insert(GameId game_id, const std::vector<CompactMove>& moves);

    /// Find games whose opening matches this exact move prefix.
    [[nodiscard]] std::vector<GameId> search_prefix(const std::vector<CompactMove>& moves) const;

    /// Count games passing through this move sequence.
    [[nodiscard]] std::uint32_t count(const std::vector<CompactMove>& moves) const;

    /// Commit any pending writes to disk.
    void save() const;

    /// Reopen the underlying database. The SQLite file is always the source
    /// of truth, so this is effectively a no-op kept for API compatibility.
    void load();

    [[nodiscard]] unsigned max_depth() const { return max_depth_; }

private:
    using NodeId = std::int64_t;
    static constexpr NodeId kRootId = 0;

    void open_db();
    void close_db();
    void ensure_schema();
    void prepare_statements();
    void finalize_statements();
    void begin_txn() const;
    void commit_txn() const;

    [[nodiscard]] NodeId walk(const std::vector<CompactMove>& moves) const;
    [[nodiscard]] NodeId walk_or_create(const std::vector<CompactMove>& moves);
    [[nodiscard]] NodeId find_child(NodeId parent, CompactMove move) const;
    [[nodiscard]] NodeId create_child(NodeId parent, CompactMove move);

    std::filesystem::path db_path_;
    unsigned              max_depth_;

    sqlite3*      db_                  = nullptr;
    sqlite3_stmt* stmt_find_child_     = nullptr;
    sqlite3_stmt* stmt_insert_node_    = nullptr;
    sqlite3_stmt* stmt_insert_edge_    = nullptr;
    sqlite3_stmt* stmt_insert_game_    = nullptr;
    sqlite3_stmt* stmt_count_games_    = nullptr;
    sqlite3_stmt* stmt_select_games_   = nullptr;

    mutable bool       txn_active_ = false;
    mutable std::mutex mutex_;
};

} // namespace tictac
