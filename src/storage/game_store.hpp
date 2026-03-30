#pragma once

#include <filesystem>
#include <mutex>
#include <vector>

#include "core/types.hpp"
#include "storage/mmap_file.hpp"

namespace tictac {

/// Append-only store for chess games.
///
/// On disk:
///   games.dat  -- serialized game blobs (append-only)
///   games.idx  -- array of uint64_t byte offsets into games.dat, indexed by GameId
class GameStore {
public:
    explicit GameStore(const std::filesystem::path& db_path);

    /// Append a game record. Returns its assigned GameId.
    GameId append(const GameRecord& game);

    /// Load a game by ID.
    [[nodiscard]] GameRecord load(GameId id) const;

    /// Number of stored games.
    [[nodiscard]] GameId count() const;

private:
    // Serialization helpers
    static std::vector<std::byte> serialize(const GameRecord& game);
    static GameRecord deserialize(const std::byte* data, std::size_t len);

    // Write a length-prefixed string
    static void write_string(std::vector<std::byte>& buf, const std::string& s);
    // Read a length-prefixed string, advancing pos
    static std::string read_string(const std::byte* data, std::size_t& pos);

    std::filesystem::path data_path_;
    std::filesystem::path index_path_;
    int data_fd_ = -1;
    std::vector<std::uint64_t> offsets_; // in-memory offset table
    mutable std::mutex write_mutex_;
};

} // namespace tictac
