#include "cli/cli_app.hpp"

#include <CLI/CLI.hpp>
#include <array>
#include <cstdio>
#include <functional>
#include <iostream>
#include <fstream>
#include <memory>
#include <sstream>

#include "engine/uci_engine.hpp"
#include "import/import_pipeline.hpp"
#include "plugin/lua_plugin.hpp"
#include "search/search_engine.hpp"
#include "storage/db_manifest.hpp"
#include "storage/game_store.hpp"
#include "storage/position_index.hpp"
#include "storage/sequence_index.hpp"
#ifdef TICTAC_HAVE_VIZ
#include "viz/viz_session.hpp"
#endif

namespace tictac {

namespace {

/// Try clipboard tools in order. Returns true if a tool ran successfully
/// (out may still be empty — caller distinguishes empty clipboard from a
/// missing-tools situation). Returns false only when no tool was usable.
bool read_clipboard(std::string& out) {
    static constexpr std::array<const char*, 3> kCmds = {
        "wl-paste --no-newline 2>/dev/null",
        "xclip -selection clipboard -o 2>/dev/null",
        "xsel --clipboard --output 2>/dev/null",
    };
    for (const char* cmd : kCmds) {
        std::FILE* fp = ::popen(cmd, "r");
        if (!fp) continue;
        std::string buf;
        char chunk[4096];
        while (auto n = std::fread(chunk, 1, sizeof(chunk), fp)) {
            buf.append(chunk, n);
        }
        int rc = ::pclose(fp);
        if (rc == 0) {
            out = std::move(buf);
            return true;
        }
        // Non-zero usually means the tool isn't available (sh exit 127) or
        // it explicitly errored on empty content (wl-paste). Try the next.
    }
    return false;
}

/// Walk down `get_subcommands()` (which lists the subcommands actually
/// matched on the command line) to find the deepest one the user reached.
/// Returns `&app` itself when no subcommand was selected.
const CLI::App* deepest_selected(const CLI::App& app) {
    const CLI::App* cur = &app;
    while (true) {
        auto picked = cur->get_subcommands();
        if (picked.empty()) return cur;
        cur = picked.front();
    }
}

/// Print top-level help plus a per-leaf section so every option (including
/// those nested two levels deep, like `search position --plugin`) is visible
/// in a single `--help` invocation.
void print_full_help(std::ostream& out, const CLI::App& app) {
    out << app.help("", CLI::AppFormatMode::All);
    auto qualify_parents = [](const CLI::App* a) {
        std::vector<std::string> parts;
        for (const CLI::App* p = a->get_parent(); p != nullptr; p = p->get_parent()) {
            parts.push_back(p->get_name());
        }
        std::string s;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!s.empty()) s += " ";
            s += *it;
        }
        return s;
    };
    std::function<void(const CLI::App&)> walk = [&](const CLI::App& a) {
        for (const auto* sub : a.get_subcommands({})) {
            if (sub->get_name().empty()) continue;
            if (sub->get_subcommands({}).empty()) {
                out << "\n" << sub->help(qualify_parents(sub));
            } else {
                walk(*sub);
            }
        }
    };
    walk(app);
}

/// Spin up a UCI engine and apply NAME=VALUE options. Returns nullptr on
/// failure, after writing a diagnostic to stderr.
std::unique_ptr<UciEngine>
make_engine(const std::filesystem::path& path,
            const std::vector<std::string>& options) {
    auto eng = std::make_unique<UciEngine>();
    if (!eng->start(path)) {
        std::cerr << "failed to start UCI engine: " << path << "\n";
        return nullptr;
    }
    for (const auto& opt : options) {
        auto eq = opt.find('=');
        if (eq == std::string::npos) {
            std::cerr << "bad --engine-option (expected NAME=VALUE): " << opt << "\n";
            return nullptr;
        }
        eng->set_option(opt.substr(0, eq), opt.substr(eq + 1));
    }
    return eng;
}

} // namespace

int CliApp::run(int argc, char* argv[]) {
    CLI::App app{"tictac - Chess PGN Database Analyzer"};
    app.require_subcommand(1);
    app.set_help_flag();
    app.set_help_all_flag("-h,--help", "Print this help message and exit");
    app.add_flag("-q,--quiet", quiet_,
                 "Suppress engine and loader progress logs (plugin output is never suppressed)");

    std::string db_path_str = "tictac_db";

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import PGN file(s) into the database");
    std::string import_input;
    import_cmd->add_option("input", import_input,
                           "PGN file, directory, or 'clipboard' to read from the system clipboard")
        ->required();
    import_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    // Search position subcommand
    auto* search_cmd = app.add_subcommand("search", "Search the database");
    search_cmd->require_subcommand(1);

    auto* search_pos = search_cmd->add_subcommand("position", "Search by board position (FEN)");
    std::string search_fen;
    std::size_t search_limit = 20;
    std::string search_plugin;
    std::string search_engine;
    std::vector<std::string> search_engine_options;
    bool search_viz = false;
    search_pos->add_option("fen", search_fen, "FEN string")->required();
    search_pos->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");
    search_pos->add_option("--limit", search_limit, "Maximum results")->default_val(20);
    search_pos->add_option("--plugin", search_plugin, "Lua plugin script (.lua) to filter results");
    search_pos->add_option("--engine", search_engine, "UCI engine binary (e.g. stockfish)");
    search_pos->add_option("--engine-option", search_engine_options,
                           "UCI option NAME=VALUE (repeatable)");
    search_pos->add_flag("--viz", search_viz,
                         "Open a Qt window after search to browse plugin-emitted FENs");

    // Search by ID subcommand
    auto* search_id_cmd = search_cmd->add_subcommand("id", "Look up a game by its assigned ID");
    std::uint32_t search_id = 0;
    search_id_cmd->add_option("--id", search_id, "Game ID")->required();
    search_id_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    // Search opening subcommand
    auto* search_open = search_cmd->add_subcommand("opening", "Search by opening moves (SAN)");
    std::vector<std::string> search_moves;
    search_open->add_option("moves", search_moves, "SAN moves (e.g. e4 e5 Nf3)")->required();
    search_open->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");
    search_open->add_option("--limit", search_limit, "Maximum results")->default_val(20);
    search_open->add_option("--plugin", search_plugin, "Lua plugin script (.lua) to filter results");
    search_open->add_option("--engine", search_engine, "UCI engine binary (e.g. stockfish)");
    search_open->add_option("--engine-option", search_engine_options,
                            "UCI option NAME=VALUE (repeatable)");
    search_open->add_flag("--viz", search_viz,
                          "Open a Qt window after search to browse plugin-emitted FENs");

    // Stats subcommand
    auto* stats_cmd = app.add_subcommand("stats", "Show database statistics");
    stats_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    // Compact subcommand
    auto* compact_cmd = app.add_subcommand("compact", "Compact position index");
    compact_cmd->add_option("--db", db_path_str, "Database path")->default_val("tictac_db");

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForAllHelp&) {
        print_full_help(std::cout, app);
        return 0;
    } catch (const CLI::ParseError& e) {
        if (std::string(e.get_name()) == "RequiredError") {
            const CLI::App* leaf = deepest_selected(app);
            if (leaf == &app) {
                // Nothing was selected — show the full overview so the user
                // can discover the available subcommands.
                print_full_help(std::cerr, app);
            } else {
                // Partial command (e.g. `tictac search`) — narrow help to
                // just that node so output is scoped to what the user typed.
                std::cerr << leaf->help();
            }
            return 0;
        }
        return app.exit(e);
    }

    std::filesystem::path db_path(db_path_str);

    if (import_cmd->parsed()) {
        return cmd_import(import_input, db_path);
    }
    if (search_pos->parsed()) {
        return cmd_search_position(search_fen, db_path, search_limit, search_plugin,
                                   search_engine, search_engine_options, search_viz);
    }
    if (search_open->parsed()) {
        return cmd_search_opening(search_moves, db_path, search_limit, search_plugin,
                                  search_engine, search_engine_options, search_viz);
    }
    if (search_id_cmd->parsed()) {
        return cmd_search_id(search_id, db_path);
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
                       const std::filesystem::path& db_path) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    DbManifest manifest(db_path);

    seq_idx.load();

    ImportPipeline pipeline(store, pos_idx, seq_idx, manifest, quiet_);

    ImportStats stats;
    if (input == "clipboard") {
        std::string pgn;
        if (!read_clipboard(pgn)) {
            std::cerr << "Could not read clipboard. Install one of: "
                         "wl-paste (wl-clipboard), xclip, xsel.\n";
            return 2;
        }
        if (pgn.empty()) {
            std::cerr << "Clipboard is empty.\n";
            return 2;
        }
        if (!quiet_) std::cout << "Importing clipboard (" << pgn.size() << " bytes):\n";
        std::istringstream iss(std::move(pgn));
        stats = pipeline.import_stream(iss);
        if (!quiet_) {
            std::cout << "  -> " << stats.games_imported << " games";
            if (stats.parse_errors > 0)
                std::cout << " (" << stats.parse_errors << " errors)";
            std::cout << "\n";
        }
    } else if (std::filesystem::is_directory(input)) {
        stats = pipeline.import_directory(input);
    } else {
        if (!quiet_) std::cout << "Importing " << input.filename() << ":\n";
        stats = pipeline.import_file(input);
        if (!quiet_) {
            if (stats.files_skipped > 0) {
                std::cout << "  skipped (already indexed)\n";
            } else {
                std::cout << "  -> " << stats.games_imported << " games";
                if (stats.parse_errors > 0)
                    std::cout << " (" << stats.parse_errors << " errors)";
                std::cout << "\n";
            }
        }
    }

    if (!quiet_) std::cout << "Compacting position index..." << std::flush;
    pos_idx.compact();
    if (!quiet_) std::cout << " done\n";

    if (!quiet_) std::cout << "Saving sequence index..." << std::flush;
    seq_idx.save();
    if (!quiet_) std::cout << " done\n";

    manifest.save();

    if (!quiet_) {
        std::cout << "Total: " << stats.games_imported << " games imported, "
                  << store.count() << " games in database\n";
    }

    return 0;
}

int CliApp::cmd_search_position(const std::string& fen,
                                const std::filesystem::path& db_path,
                                std::size_t limit,
                                const std::filesystem::path& plugin_path,
                                const std::filesystem::path& engine_path,
                                const std::vector<std::string>& engine_options,
                                bool viz) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    seq_idx.load();

    SearchEngine engine(store, pos_idx, seq_idx);

    std::unique_ptr<UciEngine> uci;
    if (!engine_path.empty()) {
        uci = make_engine(engine_path, engine_options);
        if (!uci) return 2;
    }

#ifdef TICTAC_HAVE_VIZ
    std::unique_ptr<VizSession> viz_session;
    if (viz) viz_session = std::make_unique<VizSession>();
#else
    if (viz) {
        std::cerr <<
            "--viz: this build was compiled without Qt support.\n"
            "Install Qt Widgets+Svg dev files and rebuild:\n"
            "  Debian/Ubuntu:  sudo apt install libqt5svg5-dev   (or qt6-base-dev qt6-svg-dev)\n"
            "  Fedora:         sudo dnf install qt5-qtsvg-devel  (or qt6-qtsvg-devel)\n"
            "Then ./build.sh -debug (or ./build.sh) to re-run CMake.\n";
        return 2;
    }
#endif

    std::unique_ptr<LuaPlugin> plugin;
    GameFilter filter;
    if (!plugin_path.empty()) {
        try {
#ifdef TICTAC_HAVE_VIZ
            plugin = std::make_unique<LuaPlugin>(plugin_path, uci.get(), viz_session.get());
#else
            plugin = std::make_unique<LuaPlugin>(plugin_path, uci.get());
#endif
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

#ifdef TICTAC_HAVE_VIZ
    if (viz_session && viz_session->size() > 0) viz_session->run();
#endif

    return 0;
}

int CliApp::cmd_search_opening(const std::vector<std::string>& moves,
                               const std::filesystem::path& db_path,
                               std::size_t limit,
                               const std::filesystem::path& plugin_path,
                               const std::filesystem::path& engine_path,
                               const std::vector<std::string>& engine_options,
                               bool viz) {
    GameStore store(db_path);
    PositionIndex pos_idx(db_path);
    SequenceIndex seq_idx(db_path);
    seq_idx.load();

    SearchEngine engine(store, pos_idx, seq_idx);

    std::unique_ptr<UciEngine> uci;
    if (!engine_path.empty()) {
        uci = make_engine(engine_path, engine_options);
        if (!uci) return 2;
    }

#ifdef TICTAC_HAVE_VIZ
    std::unique_ptr<VizSession> viz_session;
    if (viz) viz_session = std::make_unique<VizSession>();
#else
    if (viz) {
        std::cerr <<
            "--viz: this build was compiled without Qt support.\n"
            "Install Qt Widgets+Svg dev files and rebuild:\n"
            "  Debian/Ubuntu:  sudo apt install libqt5svg5-dev   (or qt6-base-dev qt6-svg-dev)\n"
            "  Fedora:         sudo dnf install qt5-qtsvg-devel  (or qt6-qtsvg-devel)\n"
            "Then ./build.sh -debug (or ./build.sh) to re-run CMake.\n";
        return 2;
    }
#endif

    std::unique_ptr<LuaPlugin> plugin;
    GameFilter filter;
    if (!plugin_path.empty()) {
        try {
#ifdef TICTAC_HAVE_VIZ
            plugin = std::make_unique<LuaPlugin>(plugin_path, uci.get(), viz_session.get());
#else
            plugin = std::make_unique<LuaPlugin>(plugin_path, uci.get());
#endif
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

#ifdef TICTAC_HAVE_VIZ
    if (viz_session && viz_session->size() > 0) viz_session->run();
#endif

    return 0;
}

int CliApp::cmd_search_id(std::uint32_t id, const std::filesystem::path& db_path) {
    GameStore store(db_path);

    GameRecord game;
    try {
        game = store.load(id);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }

    const char* result = "*";
    switch (game.header.result) {
        case chess::GameResult::WIN:  result = "1-0"; break;
        case chess::GameResult::LOSE: result = "0-1"; break;
        case chess::GameResult::DRAW: result = "1/2-1/2"; break;
        case chess::GameResult::NONE: result = "*"; break;
    }

    std::cout << "Game #" << game.id << ": "
              << game.header.white << " vs " << game.header.black;
    if (!game.header.event.empty()) std::cout << " [" << game.header.event << "]";
    if (!game.header.date.empty()) std::cout << " " << game.header.date;
    std::cout << "\n";
    std::cout << "  Result: " << result << "\n";
    if (game.header.white_elo || game.header.black_elo) {
        std::cout << "  Elo:    " << game.header.white_elo
                  << " vs " << game.header.black_elo << "\n";
    }
    std::cout << "  Moves:  " << game.moves.size() << "\n";

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
