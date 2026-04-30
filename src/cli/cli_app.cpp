#include "cli/cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <fstream>
#include <memory>

#include "import/import_pipeline.hpp"
#include "plugin/lua_plugin.hpp"
#include "search/search_engine.hpp"
#include "storage/db_manifest.hpp"
#include "storage/game_store.hpp"
#include "storage/position_index.hpp"
#include "storage/sequence_index.hpp"

namespace tictac {

int CliApp::run(int argc, char* argv[]) {
    CLI::App app{"tictac - Chess PGN Database Analyzer"};
    app.require_subcommand(1);

    std::string db_path_str = "tictac_db";

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import PGN file(s) into the database");
    std::string import_input;
    unsigned import_threads = 1;
    import_cmd->add_option("input", import_input, "PGN file or directory")->required();
    import_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");
    import_cmd->add_option("--threads", import_threads, "Number of threads")->default_val(1);

    // Search position subcommand
    auto* search_cmd = app.add_subcommand("search", "Search the database");
    search_cmd->require_subcommand(1);

    auto* search_pos = search_cmd->add_subcommand("position", "Search by board position (FEN)");
    std::string search_fen;
    std::size_t search_limit = 20;
    std::string search_plugin;
    search_pos->add_option("fen", search_fen, "FEN string")->required();
    search_pos->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");
    search_pos->add_option("--limit", search_limit, "Maximum results")->default_val(20);
    search_pos->add_option("--plugin", search_plugin, "Lua plugin script (.lua) to filter results");

    // Search opening subcommand
    auto* search_open = search_cmd->add_subcommand("opening", "Search by opening moves (SAN)");
    std::vector<std::string> search_moves;
    search_open->add_option("moves", search_moves, "SAN moves (e.g. e4 e5 Nf3)")->required();
    search_open->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");
    search_open->add_option("--limit", search_limit, "Maximum results")->default_val(20);
    search_open->add_option("--plugin", search_plugin, "Lua plugin script (.lua) to filter results");

    // Stats subcommand
    auto* stats_cmd = app.add_subcommand("stats", "Show database statistics");
    stats_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    // Compact subcommand
    auto* compact_cmd = app.add_subcommand("compact", "Compact position index");
    compact_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    CLI11_PARSE(app, argc, argv);

    std::filesystem::path db_path(db_path_str);

    if (import_cmd->parsed()) {
        return cmd_import(import_input, db_path, import_threads);
    }
    if (search_pos->parsed()) {
        return cmd_search_position(search_fen, db_path, search_limit, search_plugin);
    }
    if (search_open->parsed()) {
        return cmd_search_opening(search_moves, db_path, search_limit, search_plugin);
    }
    if (stats_cmd->parsed()) {
        return cmd_stats(db_path);
    }
    if (compact_cmd->parsed()) {
        return cmd_compact(db_path);
    }

    return 0;
}

int CliApp::cmd_import(const std::filesystem::path& input,
                       const std::filesystem::path& db_path,
                       [[maybe_unused]] unsigned threads) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    DbManifest manifest(db_path);

    seq_idx.load();

    ImportPipeline pipeline(store, pos_idx, seq_idx, manifest);

    ImportStats stats;
    if (std::filesystem::is_directory(input)) {
        stats = pipeline.import_directory(input);
    } else {
        std::cout << "Importing " << input.filename() << "..." << std::flush;
        stats = pipeline.import_file(input);
        if (stats.files_skipped > 0) {
            std::cout << " skipped (already indexed)\n";
        } else {
            std::cout << " " << stats.games_imported << " games";
            if (stats.parse_errors > 0)
                std::cout << " (" << stats.parse_errors << " errors)";
            std::cout << "\n";
        }
    }

    std::cout << "Compacting position index..." << std::flush;
    pos_idx.compact();
    std::cout << " done\n";

    std::cout << "Saving sequence index..." << std::flush;
    seq_idx.save();
    std::cout << " done\n";

    manifest.save();

    std::cout << "Total: " << stats.games_imported << " games imported, "
              << store.count() << " games in database\n";

    return 0;
}

int CliApp::cmd_search_position(const std::string& fen,
                                const std::filesystem::path& db_path,
                                std::size_t limit,
                                const std::filesystem::path& plugin_path) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    seq_idx.load();

    SearchEngine engine(store, pos_idx, seq_idx);

    std::unique_ptr<LuaPlugin> plugin;
    GameFilter filter;
    if (!plugin_path.empty()) {
        try {
            plugin = std::make_unique<LuaPlugin>(plugin_path);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 2;
        }
        filter = [&](const GameRecord& g, std::optional<HalfMoveIdx> ply) {
            return plugin->on_match(g, ply);
        };
    }

    std::vector<PositionSearchResult> results;
    try {
        results = engine.search_position(fen, limit, filter);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }

    if (results.empty()) {
        std::cout << "No games found with this position.\n";
        return 0;
    }

    std::cout << results.size() << " game(s) found:\n\n";
    for (const auto& r : results) {
        std::cout << "  Game #" << r.game_id << " (ply " << r.ply << "): "
                  << r.header.white << " vs " << r.header.black;
        if (!r.header.event.empty()) std::cout << " [" << r.header.event << "]";
        if (!r.header.date.empty()) std::cout << " " << r.header.date;
        std::cout << "\n";
    }

    return 0;
}

int CliApp::cmd_search_opening(const std::vector<std::string>& moves,
                               const std::filesystem::path& db_path,
                               std::size_t limit,
                               const std::filesystem::path& plugin_path) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    seq_idx.load();

    SearchEngine engine(store, pos_idx, seq_idx);

    std::unique_ptr<LuaPlugin> plugin;
    GameFilter filter;
    if (!plugin_path.empty()) {
        try {
            plugin = std::make_unique<LuaPlugin>(plugin_path);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 2;
        }
        filter = [&](const GameRecord& g, std::optional<HalfMoveIdx> ply) {
            return plugin->on_match(g, ply);
        };
    }

    std::vector<SequenceSearchResult> results;
    std::uint32_t freq = 0;
    try {
        results = engine.search_opening(moves, limit, filter);
        freq = engine.opening_frequency(moves);
    } catch (const std::exception& e) {
        std::cerr << "Invalid opening moves: " << e.what() << "\n";
        return 2;
    }

    if (results.empty()) {
        std::cout << "No games found with this opening.\n";
        return 0;
    }


    std::cout << freq << " game(s) total, showing " << results.size() << ":\n\n";
    for (const auto& r : results) {
        std::cout << "  Game #" << r.game_id << ": "
                  << r.header.white << " vs " << r.header.black;
        if (!r.header.event.empty()) std::cout << " [" << r.header.event << "]";
        if (!r.header.date.empty()) std::cout << " " << r.header.date;
        std::cout << "\n";
    }

    return 0;
}

int CliApp::cmd_stats(const std::filesystem::path& db_path) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);

    std::cout << "Database: " << db_path << "\n";
    std::cout << "Games:    " << store.count() << "\n";
    std::cout << "Position entries: " << pos_idx.total_entries() << "\n";

    return 0;
}

int CliApp::cmd_compact(const std::filesystem::path& db_path) {
    PositionIndex pos_idx(db_path);

    std::cout << "Compacting position index..." << std::flush;
    pos_idx.compact();
    std::cout << " done\n";
    std::cout << "Total position entries: " << pos_idx.total_entries() << "\n";

    return 0;
}

} // namespace tictac
