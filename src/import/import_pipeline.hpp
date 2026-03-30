#pragma once

#include <cstdint>
#include <filesystem>

namespace tictac {

class GameStore;
class PositionIndex;
class SequenceIndex;
class DbManifest;

struct ImportStats {
    std::uint64_t games_imported   = 0;
    std::uint64_t positions_indexed = 0;
    std::uint64_t parse_errors     = 0;
    std::uint64_t files_skipped    = 0;
};

/// Orchestrates PGN import with manifest-based deduplication.
/// Parsing is single-threaded per file (PGN is inherently sequential),
/// but position indexing happens inline for simplicity.
class ImportPipeline {
public:
    ImportPipeline(GameStore& store, PositionIndex& pos_idx,
                   SequenceIndex& seq_idx, DbManifest& manifest);

    /// Import a single PGN file.
    ImportStats import_file(const std::filesystem::path& pgn_path);

    /// Import all .pgn files in a directory.
    ImportStats import_directory(const std::filesystem::path& dir);

private:
    GameStore&     store_;
    PositionIndex& pos_idx_;
    SequenceIndex& seq_idx_;
    DbManifest&    manifest_;
};

} // namespace tictac
