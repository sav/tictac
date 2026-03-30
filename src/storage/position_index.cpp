#include "storage/position_index.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace tictac {

PositionIndex::PositionIndex(const std::filesystem::path& db_path) {
    index_dir_ = db_path / "pos_index";
    std::filesystem::create_directories(index_dir_);
}

void PositionIndex::insert(ZobristKey key, GameId game_id, HalfMoveIdx ply) {
    std::lock_guard lock(wal_mutex_);
    wal_buffer_.push_back({key, game_id, ply});
}

void PositionIndex::compact() {
    std::vector<WalEntry> wal;
    {
        std::lock_guard lock(wal_mutex_);
        wal.swap(wal_buffer_);
    }
    if (wal.empty()) return;

    // Group WAL entries by shard
    std::vector<std::vector<PosIndexEntry>> per_shard(NUM_SHARDS);
    for (const auto& w : wal) {
        unsigned s = shard_of(w.full_key);
        per_shard[s].push_back({suffix_of(w.full_key), w.game_id, w.ply});
    }

    // Merge each non-empty shard
    for (unsigned s = 0; s < NUM_SHARDS; ++s) {
        if (per_shard[s].empty()) continue;

        // Read existing shard
        auto existing = read_shard(s);

        // Sort new entries
        std::sort(per_shard[s].begin(), per_shard[s].end());

        // Merge
        std::vector<PosIndexEntry> merged;
        merged.reserve(existing.size() + per_shard[s].size());
        std::merge(existing.begin(), existing.end(),
                   per_shard[s].begin(), per_shard[s].end(),
                   std::back_inserter(merged));

        write_shard(s, merged);
    }
}

std::vector<PositionOccurrence> PositionIndex::lookup(ZobristKey key, std::size_t max_results) const {
    auto shard = shard_of(key);
    auto suffix = suffix_of(key);

    // Search on-disk shard
    auto entries = read_shard(shard);

    PosIndexEntry needle{};
    needle.zobrist_suffix = suffix;

    auto lower = std::lower_bound(entries.begin(), entries.end(), needle,
        [](const PosIndexEntry& a, const PosIndexEntry& b) {
            return a.zobrist_suffix < b.zobrist_suffix;
        });

    std::vector<PositionOccurrence> results;
    for (auto it = lower; it != entries.end() && it->zobrist_suffix == suffix; ++it) {
        results.push_back({it->game_id, it->ply});
        if (max_results > 0 && results.size() >= max_results) break;
    }

    // Also search WAL
    {
        std::lock_guard lock(wal_mutex_);
        for (const auto& w : wal_buffer_) {
            if (shard_of(w.full_key) == shard && suffix_of(w.full_key) == suffix) {
                results.push_back({w.game_id, w.ply});
                if (max_results > 0 && results.size() >= max_results) break;
            }
        }
    }

    return results;
}

std::size_t PositionIndex::count(ZobristKey key) const {
    return lookup(key).size();
}

std::size_t PositionIndex::total_entries() const {
    std::size_t total = 0;

    for (unsigned s = 0; s < NUM_SHARDS; ++s) {
        auto path = shard_path(s);
        if (std::filesystem::exists(path))
            total += std::filesystem::file_size(path) / sizeof(PosIndexEntry);
    }

    std::lock_guard lock(wal_mutex_);
    total += wal_buffer_.size();
    return total;
}

std::filesystem::path PositionIndex::shard_path(unsigned shard) const {
    char name[20];
    std::snprintf(name, sizeof(name), "shard_%04x.dat", shard);
    return index_dir_ / name;
}

std::vector<PosIndexEntry> PositionIndex::read_shard(unsigned shard) const {
    auto path = shard_path(shard);
    if (!std::filesystem::exists(path))
        return {};

    auto file_size = std::filesystem::file_size(path);
    if (file_size == 0)
        return {};

    auto entry_count = file_size / sizeof(PosIndexEntry);
    std::vector<PosIndexEntry> entries(entry_count);

    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(entries.data()),
              static_cast<std::streamsize>(file_size));

    return entries;
}

void PositionIndex::write_shard(unsigned shard, const std::vector<PosIndexEntry>& entries) const {
    auto path = shard_path(shard);
    auto tmp = path;
    tmp += ".tmp";

    {
        std::ofstream file(tmp, std::ios::binary);
        file.write(reinterpret_cast<const char*>(entries.data()),
                   static_cast<std::streamsize>(entries.size() * sizeof(PosIndexEntry)));
    }

    std::filesystem::rename(tmp, path);
}

} // namespace tictac
