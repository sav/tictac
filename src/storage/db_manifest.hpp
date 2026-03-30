#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace tictac {

/// Tracks which PGN files have been indexed, enabling incremental import.
/// Stored as a simple text-based manifest file.
class DbManifest {
public:
    explicit DbManifest(const std::filesystem::path& db_path);

    /// Check if a file (by path + size + mtime) is already indexed.
    [[nodiscard]] bool is_file_indexed(const std::filesystem::path& pgn_path) const;

    /// Mark a file as indexed.
    void mark_file_indexed(const std::filesystem::path& pgn_path,
                           GameId first_id, GameId last_id);

    /// Get the next game ID to use.
    [[nodiscard]] GameId next_game_id() const;

    /// Persist to disk.
    void save() const;

    /// Load from disk.
    void load();

private:
    struct FileEntry {
        std::string canonical_path;
        std::uintmax_t file_size = 0;
        GameId first_id = 0;
        GameId last_id  = 0;
    };

    std::filesystem::path manifest_path_;
    std::vector<FileEntry> entries_;
};

} // namespace tictac
