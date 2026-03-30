#include "import/import_pipeline.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include "chess.hpp"
#include "import/import_visitor.hpp"
#include "storage/db_manifest.hpp"
#include "storage/game_store.hpp"
#include "storage/position_index.hpp"
#include "storage/sequence_index.hpp"

namespace tictac {

ImportPipeline::ImportPipeline(GameStore& store, PositionIndex& pos_idx,
                               SequenceIndex& seq_idx, DbManifest& manifest)
    : store_(store), pos_idx_(pos_idx), seq_idx_(seq_idx), manifest_(manifest)
{}

ImportStats ImportPipeline::import_file(const std::filesystem::path& pgn_path) {
    ImportStats stats;

    if (manifest_.is_file_indexed(pgn_path)) {
        stats.files_skipped++;
        return stats;
    }

    GameId first_id = store_.count();

    auto process_game = [&](GameRecord&& game) {
        auto id = store_.append(game);

        // Index all positions
        chess::Board board;
        pos_idx_.insert(board.hash(), id, 0);
        for (HalfMoveIdx ply = 0; ply < game.moves.size(); ++ply) {
            chess::Move m(game.moves[ply]);
            board.makeMove(m);
            pos_idx_.insert(board.hash(), id, static_cast<HalfMoveIdx>(ply + 1));
        }
        stats.positions_indexed += game.moves.size() + 1;

        // Index opening sequence
        seq_idx_.insert(id, game.moves);

        stats.games_imported++;
    };

    ImportVisitor visitor(process_game);
    std::ifstream file(pgn_path);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << pgn_path << "\n";
        return stats;
    }

    chess::pgn::StreamParser parser(file);
    parser.readGames(visitor);

    stats.parse_errors = visitor.parse_errors();

    GameId last_id = store_.count() > 0 ? store_.count() - 1 : 0;
    manifest_.mark_file_indexed(pgn_path, first_id, last_id);

    return stats;
}

ImportStats ImportPipeline::import_directory(const std::filesystem::path& dir) {
    ImportStats total_stats;

    std::vector<std::filesystem::path> pgn_files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".pgn")
            pgn_files.push_back(entry.path());
    }
    std::sort(pgn_files.begin(), pgn_files.end());

    for (const auto& pgn : pgn_files) {
        std::cout << "Importing " << pgn.filename() << "..." << std::flush;

        auto stats = import_file(pgn);

        if (stats.files_skipped > 0) {
            std::cout << " skipped (already indexed)\n";
            total_stats.files_skipped++;
        } else {
            std::cout << " " << stats.games_imported << " games";
            if (stats.parse_errors > 0) std::cout << " (" << stats.parse_errors << " errors)";
            std::cout << "\n";

            total_stats.games_imported += stats.games_imported;
            total_stats.positions_indexed += stats.positions_indexed;
            total_stats.parse_errors += stats.parse_errors;
        }
    }

    return total_stats;
}

} // namespace tictac
