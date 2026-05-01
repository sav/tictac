// CLI integration tests — drive CliApp::run() in-process and assert on
// captured stdout/stderr. Stockfish- and clipboard-dependent cases skip
// cleanly when the host doesn't provide the tool.

#include <catch2/catch_test_macros.hpp>

#include "cli/cli_app.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = fs::path(__FILE__).parent_path() / "fixtures";

struct CliResult {
    int rc = 0;
    std::string out;
    std::string err;
};

// Redirects fd 1 and fd 2 (stdout/stderr) into temp files so that BOTH C++
// streams (std::cout) and C streams (printf, FILE* stdout — which Lua's
// io.write uses) are captured. Restoring is two-step (`restore()` swaps the
// fds back; the destructor also handles it on scope exit) so callers can
// drain the temp files between restoration and destruction.
class StreamRedirect {
public:
    StreamRedirect() {
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);

        out_file_ = std::tmpfile();
        err_file_ = std::tmpfile();
        REQUIRE(out_file_);
        REQUIRE(err_file_);

        saved_out_ = ::dup(STDOUT_FILENO);
        saved_err_ = ::dup(STDERR_FILENO);
        REQUIRE(saved_out_ >= 0);
        REQUIRE(saved_err_ >= 0);

        ::dup2(::fileno(out_file_), STDOUT_FILENO);
        ::dup2(::fileno(err_file_), STDERR_FILENO);
    }

    ~StreamRedirect() {
        if (saved_out_ >= 0) restore();
        if (out_file_) std::fclose(out_file_);
        if (err_file_) std::fclose(err_file_);
    }

    StreamRedirect(const StreamRedirect&) = delete;
    StreamRedirect& operator=(const StreamRedirect&) = delete;

    void restore() {
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        ::dup2(saved_out_, STDOUT_FILENO);
        ::dup2(saved_err_, STDERR_FILENO);
        ::close(saved_out_);
        ::close(saved_err_);
        saved_out_ = -1;
        saved_err_ = -1;
    }

    std::string out() { return read_all(out_file_); }
    std::string err() { return read_all(err_file_); }

private:
    static std::string read_all(std::FILE* f) {
        std::fflush(f);
        std::rewind(f);
        std::string s;
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        return s;
    }

    std::FILE* out_file_ = nullptr;
    std::FILE* err_file_ = nullptr;
    int saved_out_ = -1;
    int saved_err_ = -1;
};

CliResult run_cli(std::vector<std::string> args) {
    std::string program = "tictac";
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(program.data());
    for (auto& a : args) argv.push_back(a.data());

    StreamRedirect sr;
    tictac::CliApp app;
    int rc = app.run(static_cast<int>(argv.size()), argv.data());
    sr.restore();
    return {rc, sr.out(), sr.err()};
}

fs::path fresh_dir(std::string_view tag) {
    static std::atomic<unsigned> counter{0};
    auto p = fs::temp_directory_path() /
             ("tictac_cli_" + std::string(tag) + "_" +
              std::to_string(counter.fetch_add(1)));
    fs::remove_all(p);
    return p;
}

bool tool_available(const char* probe) {
    return std::system(probe) == 0;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

} // namespace

// ---------- import ----------

TEST_CASE("import <file> ingests games and reports a total", "[cli][import]") {
    auto db = fresh_dir("import_file");
    auto r = run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                      "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "Importing"));
    CHECK(contains(r.out, "3 games"));
    CHECK(contains(r.out, "Total: 3 games imported"));
    // Per-game lines are intentionally not printed during import; game
    // details are only emitted by the `search` commands.
    CHECK_FALSE(contains(r.out, "Alpha vs Beta"));
    fs::remove_all(db);
}

TEST_CASE("import --quiet suppresses loader chatter", "[cli][import][quiet]") {
    auto db = fresh_dir("import_quiet");
    auto r = run_cli({"--quiet", "import", (kFixtures / "multi_game.pgn").string(),
                      "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(r.out.empty());

    // Database is still populated — the silence is cosmetic only.
    auto stats = run_cli({"stats", "--db", db.string()});
    REQUIRE(stats.rc == 0);
    CHECK(contains(stats.out, "Games:    3"));
    fs::remove_all(db);
}

TEST_CASE("-q is the short form of --quiet", "[cli][import][quiet]") {
    auto db = fresh_dir("import_quiet_short");
    auto r = run_cli({"-q", "import", (kFixtures / "multi_game.pgn").string(),
                      "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(r.out.empty());
    fs::remove_all(db);
}

TEST_CASE("import is idempotent — re-import skips file via manifest", "[cli][import]") {
    auto db = fresh_dir("import_skip");
    auto file = (kFixtures / "multi_game.pgn").string();
    REQUIRE(run_cli({"import", file, "--db", db.string()}).rc == 0);

    auto r2 = run_cli({"import", file, "--db", db.string()});
    REQUIRE(r2.rc == 0);
    CHECK(contains(r2.out, "skipped (already indexed)"));
    CHECK(contains(r2.out, "Total: 0 games imported"));
    fs::remove_all(db);
}

TEST_CASE("import <dir> recurses pgn files in stable order", "[cli][import]") {
    auto db  = fresh_dir("import_dir");
    auto dir = fresh_dir("import_dir_input");
    fs::create_directories(dir);
    fs::copy_file(kFixtures / "multi_game.pgn",  dir / "multi_game.pgn");
    fs::copy_file(kFixtures / "simple_game.pgn", dir / "simple_game.pgn");

    auto r = run_cli({"import", dir.string(), "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "multi_game.pgn"));
    CHECK(contains(r.out, "simple_game.pgn"));
    CHECK(contains(r.out, "Total: 4 games imported"));
    fs::remove_all(db);
    fs::remove_all(dir);
}

TEST_CASE("import clipboard reads PGN from the system clipboard", "[cli][import][clipboard]") {
    if (!tool_available("which xclip >/dev/null 2>&1")) {
        SKIP("xclip not installed; cannot prime clipboard");
    }
    auto file = (kFixtures / "multi_game.pgn").string();
    auto load = "xclip -selection clipboard -i < " + file;
    if (std::system(load.c_str()) != 0) {
        SKIP("xclip failed (no DISPLAY?)");
    }

    auto db = fresh_dir("import_clipboard");
    auto r = run_cli({"import", "clipboard", "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "Importing clipboard"));
    CHECK(contains(r.out, "Total: 3 games imported"));

    // Clipboard imports bypass the manifest — re-running re-imports the games.
    auto r2 = run_cli({"import", "clipboard", "--db", db.string()});
    REQUIRE(r2.rc == 0);
    CHECK(contains(r2.out, "Total: 3 games imported"));
    fs::remove_all(db);
}

// ---------- search ----------

TEST_CASE("partial command prints only the matched subcommand's help", "[cli][help]") {
    // `tictac search` (no sub-sub) should narrow to the search node — list
    // its three children and *not* leak peer subcommands like `import` or
    // `stats` from the top-level overview.
    auto r = run_cli({"search"});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.err, "Search the database"));
    CHECK(contains(r.err, "position"));
    CHECK(contains(r.err, "opening"));
    CHECK(contains(r.err, "id"));
    CHECK_FALSE(contains(r.err, "Import PGN"));
    CHECK_FALSE(contains(r.err, "Show database statistics"));
}

TEST_CASE("bare invocation prints the full overview", "[cli][help]") {
    // No subcommand selected — keep the discovery view that lists every
    // top-level command so newcomers can find their way around.
    auto r = run_cli({});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.err, "tictac - Chess PGN Database Analyzer"));
    CHECK(contains(r.err, "import"));
    CHECK(contains(r.err, "search"));
    CHECK(contains(r.err, "stats"));
    CHECK(contains(r.err, "compact"));
}

TEST_CASE("search opening matches by SAN prefix and reports 'no games' otherwise", "[cli][search]") {
    auto db = fresh_dir("search_open");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto e4 = run_cli({"search", "opening", "e4", "--db", db.string()});
    REQUIRE(e4.rc == 0);
    CHECK(contains(e4.out, "2 game(s) total"));

    auto d4 = run_cli({"search", "opening", "d4", "--db", db.string()});
    REQUIRE(d4.rc == 0);
    CHECK(contains(d4.out, "Gamma vs Delta"));

    auto none = run_cli({"search", "opening", "c4", "--db", db.string()});
    REQUIRE(none.rc == 0);
    CHECK(contains(none.out, "No games found with this opening."));
    fs::remove_all(db);
}

TEST_CASE("search opening rejects non-SAN tokens with stderr message", "[cli][search]") {
    auto db = fresh_dir("search_bad_san");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto bad = run_cli({"search", "opening", "B50", "--db", db.string()});
    CHECK(bad.rc != 0);
    CHECK(contains(bad.err, "Invalid opening moves"));
    fs::remove_all(db);
}

TEST_CASE("search position finds games at a specific FEN", "[cli][search]") {
    auto db = fresh_dir("search_pos");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    // Position right after 1.d4 — only Game #1 (Gamma vs Delta) reaches it.
    auto fen = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1";
    auto r = run_cli({"search", "position", fen, "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "1 game(s) found"));
    CHECK(contains(r.out, "Gamma vs Delta"));
    fs::remove_all(db);
}

TEST_CASE("search id loads a game by its assigned ID", "[cli][search]") {
    auto db = fresh_dir("search_id");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto r = run_cli({"search", "id", "--id", "1", "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "Game #1"));
    CHECK(contains(r.out, "Gamma vs Delta"));
    CHECK(contains(r.out, "0-1"));

    auto bad = run_cli({"search", "id", "--id", "99", "--db", db.string()});
    CHECK(bad.rc != 0);
    CHECK(contains(bad.err, "invalid GameId"));
    fs::remove_all(db);
}

// ---------- search --plugin ----------

TEST_CASE("search opening --plugin filters via Lua on_match", "[cli][search][lua]") {
    auto db = fresh_dir("search_plugin");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "filter_alpha.lua").string();
    auto r = run_cli({"search", "opening", "e4",
                      "--db", db.string(), "--plugin", plugin});
    REQUIRE(r.rc == 0);
    // Only Game 0 (Alpha vs Beta) survives the filter; Game 2 (Epsilon) is dropped.
    CHECK(contains(r.out, "Alpha vs Beta"));
    CHECK_FALSE(contains(r.out, "Epsilon vs Zeta"));
    fs::remove_all(db);
}

TEST_CASE("search opening --plugin exposes game:moves() with san+uci", "[cli][search][lua]") {
    auto db = fresh_dir("search_plugin_moves");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "move_iter.lua").string();
    auto r = run_cli({"search", "opening", "e4", "e5",
                      "--db", db.string(), "--plugin", plugin});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "first-move"));
    CHECK(contains(r.out, "san=e4"));
    CHECK(contains(r.out, "uci=e2e4"));
    fs::remove_all(db);
}

TEST_CASE("search opening --plugin exposes game:fen() at any ply", "[cli][search][lua]") {
    auto db = fresh_dir("search_plugin_fen");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "fen_iter.lua").string();
    auto r = run_cli({"search", "opening", "e4", "e5",
                      "--db", db.string(), "--plugin", plugin});
    REQUIRE(r.rc == 0);

    // ply=0 is the standard chess starting position.
    CHECK(contains(r.out,
        "start=rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

    // ply=1 is the position after 1.e4 (en-passant target e3).
    CHECK(contains(r.out, "ply1=rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR"));

    // Final position of Alpha vs Beta (1.e4 e5 2.Nf3 Nc6 3.Bb5).
    CHECK(contains(r.out, "end=r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R"));

    // No match ply on opening search → game:fen() falls back to ply 0.
    CHECK(contains(r.out, "default=rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"));
    fs::remove_all(db);
}

TEST_CASE("search --plugin errors on missing on_match", "[cli][search][lua]") {
    auto db = fresh_dir("search_plugin_bad");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "no_on_match.lua").string();
    auto r = run_cli({"search", "opening", "e4",
                      "--db", db.string(), "--plugin", plugin});
    CHECK(r.rc != 0);
    CHECK(contains(r.err, "on_match"));
    fs::remove_all(db);
}

TEST_CASE("search --plugin errors on missing script file", "[cli][search][lua]") {
    auto db = fresh_dir("search_plugin_missing");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto r = run_cli({"search", "opening", "e4",
                      "--db", db.string(), "--plugin", "/nonexistent.lua"});
    CHECK(r.rc != 0);
    CHECK(contains(r.err, "Lua plugin"));
    fs::remove_all(db);
}

// ---------- search --engine ----------

TEST_CASE("search --engine evaluates positions with a UCI engine", "[cli][search][engine]") {
    if (!tool_available("which stockfish >/dev/null 2>&1")) {
        SKIP("stockfish not installed");
    }

    auto db = fresh_dir("search_engine");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "engine_label.lua").string();
    auto r = run_cli({"search", "opening", "e4",
                      "--db", db.string(),
                      "--plugin", plugin,
                      "--engine", "stockfish",
                      "--engine-option", "Threads=1"});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "eval="));
    fs::remove_all(db);
}

TEST_CASE("engine flag bails when the binary cannot be started", "[cli][search][engine]") {
    auto db = fresh_dir("search_engine_bad");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto plugin = (kFixtures / "filter_alpha.lua").string();
    auto r = run_cli({"search", "opening", "e4",
                      "--db", db.string(),
                      "--plugin", plugin,
                      "--engine", "/nonexistent/uci-engine"});
    CHECK(r.rc != 0);
    CHECK(contains(r.err, "failed to start UCI engine"));
    fs::remove_all(db);
}

// ---------- stats / compact ----------

TEST_CASE("stats reports the database path, game count, and entry count", "[cli][stats]") {
    auto db = fresh_dir("stats");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto r = run_cli({"stats", "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "Database:"));
    CHECK(contains(r.out, "Games:    3"));
    CHECK(contains(r.out, "Position entries:"));
    fs::remove_all(db);
}

TEST_CASE("compact runs idempotently after import", "[cli][compact]") {
    auto db = fresh_dir("compact");
    REQUIRE(run_cli({"import", (kFixtures / "multi_game.pgn").string(),
                     "--db", db.string()}).rc == 0);

    auto r = run_cli({"compact", "--db", db.string()});
    REQUIRE(r.rc == 0);
    CHECK(contains(r.out, "Compacting position index"));
    CHECK(contains(r.out, "Total position entries:"));
    fs::remove_all(db);
}
