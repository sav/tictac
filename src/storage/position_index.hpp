#pragma once

#include <filesystem>
#include <mutex>
#include <vector>

#include "core/types.hpp"

namespace tictac {

/// On-disk entry in a position index shard (sorted by suffix).
struct __attribute__((packed)) PosIndexEntry {
    std::uint64_t zobrist_suffix; // lower 52 bits of Zobrist key
    GameId        game_id;
    HalfMoveIdx   ply;

    bool operator<(const PosIndexEntry& o) const {
        if (zobrist_suffix != o.zobrist_suffix) return zobrist_suffix < o.zobrist_suffix;
        if (game_id != o.game_id) return game_id < o.game_id;
        return ply < o.ply;
    }
};
static_assert(sizeof(PosIndexEntry) == 14);

/// WAL entry: full key so we can recover shard at compact time.
struct WalEntry {
    ZobristKey  full_key;
    GameId      game_id;
    HalfMoveIdx ply;
};

/// Sharded, sorted-array position index.
///
/// Positions are sharded by the top SHARD_BITS bits of the Zobrist key.
/// Each shard is a sorted file of PosIndexEntry records.
/// Writes go to an in-memory WAL buffer, flushed to per-shard files on compact().
class PositionIndex {
public:
    explicit PositionIndex(const std::filesystem::path& db_path);

    /// Insert a position occurrence into the WAL.
    void insert(ZobristKey key, GameId game_id, HalfMoveIdx ply);

    /// Flush WAL buffer to disk and merge into sorted shard files.
    void compact();

    /// Look up all occurrences of a position by Zobrist key.
    [[nodiscard]] std::vector<PositionOccurrence> lookup(ZobristKey key, std::size_t max_results = 0) const;

    /// Count occurrences of a position.
    [[nodiscard]] std::size_t count(ZobristKey key) const;

    /// Total entries across all shards + WAL.
    [[nodiscard]] std::size_t total_entries() const;

    static constexpr unsigned SHARD_BITS = 12;
    static constexpr unsigned NUM_SHARDS = 1u << SHARD_BITS;
    static constexpr std::uint64_t SUFFIX_MASK = (1ULL << 52) - 1;

    static unsigned shard_of(ZobristKey key) { return static_cast<unsigned>(key >> 52); }
    static std::uint64_t suffix_of(ZobristKey key) { return key & SUFFIX_MASK; }

private:
    std::filesystem::path shard_path(unsigned shard) const;

    /// Read existing shard entries from disk.
    std::vector<PosIndexEntry> read_shard(unsigned shard) const;

    /// Write sorted entries to a shard file (atomic replace).
    void write_shard(unsigned shard, const std::vector<PosIndexEntry>& entries) const;

    std::filesystem::path index_dir_;
    mutable std::mutex wal_mutex_;
    std::vector<WalEntry> wal_buffer_;
};

} // namespace tictac
