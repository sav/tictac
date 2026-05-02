# tictac

Chess PGN database analyzer. The name nods at "tactic" — the original motivation was mining PGN archives for fresh tactical puzzles. Imports PGN files into an on-disk store, then answers two kinds of queries fast:

- **Position search** — given a FEN, find games that reach that exact board state.
- **Opening search** — given a sequence of SAN moves, find games whose opening matches that prefix.

It is a single C++20 binary. State lives entirely in a directory of mmap-backed files; there is no daemon, server, or external database.

## Status

Pre-1.0. The on-disk format is not committed-to; rebuild the database after pulling. Tested on Linux. Imports are currently single-threaded.

## Requirements

- C++20 compiler (GCC 11+ or Clang 14+)
- C compiler (Lua 5.4 is built from source)
- CMake 3.20+
- SQLite 3 development headers (`libsqlite3-dev` on Debian/Ubuntu, `sqlite-devel` on Fedora)
- Internet access on the first configure (CMake `FetchContent` pulls the chess library, Catch2, and Lua 5.4)
- *(Optional)* Qt6 or Qt5 Widgets dev files for the `--viz` board browser
  (`qt6-base-dev` or `qtbase5-dev` on Debian/Ubuntu). Pass
  `-DTICTAC_BUILD_VIZ=OFF` to skip the dependency.

## Build

Two out-of-tree build directories are conventional in this repo:

| Build   | Directory     | Use for                          |
| ------- | ------------- | -------------------------------- |
| Release | `build/`      | Imports, large-scale searches    |
| Debug   | `build-debug/`| Diagnosing issues, running tests |

Direct CMake:

```sh
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Debug
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
```

Or use the wrapper script:

```sh
./build.sh           # Release (default)
./build.sh -debug    # Debug
```

The binary lands at `build/src/tictac` (or `build-debug/src/tictac`).

### CMake options

| Option                       | Default | Description                                        |
| ---------------------------- | ------- | -------------------------------------------------- |
| `TICTAC_BUILD_TESTS`         | `ON`    | Build the Catch2 test suite                        |
| `TICTAC_BUILD_BENCH`         | `OFF`   | Build benchmarks (under `bench/`)                  |
| `TICTAC_ENABLE_SANITIZERS`   | `OFF`   | Compile with ASan + UBSan                          |
| `TICTAC_BUILD_VIZ`           | `ON`    | Build the Qt board browser (`--viz`); needs Qt5/6  |

Example with sanitizers:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DTICTAC_ENABLE_SANITIZERS=ON
cmake --build build-asan -j
```

## Quick start

```sh
# 1. Build (Release)
./build.sh

# 2. Import a PGN file, a directory of PGN files, or the clipboard
./build/src/tictac import path/to/games.pgn --db mydb
./build/src/tictac import clipboard --db mydb

# 3. Search by opening (SAN moves, space-separated)
./build/src/tictac search opening e4 c5 Nf3 d6 --db mydb

# 4. Search by position (FEN)
./build/src/tictac search position \
  "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2" \
  --db mydb

# 5. Show database stats
./build/src/tictac stats --db mydb
```

If `--db` is omitted everywhere, tictac uses `./tictac_db` in the current working directory.

## CLI reference

### Global flags

- `-q`, `--quiet` — suppress progress chatter from the loader (import) and the
  engine subprocess. Search results, stats, and any output produced by a Lua
  plugin (`print`, `io.write`) are **never** suppressed; the flag only mutes
  housekeeping noise. Place it before the subcommand: `tictac -q import …`.

### `import` — ingest PGN

```
tictac import <input> [--db PATH]
```

- `<input>` — one of:
  - a `.pgn` file,
  - a directory containing `.pgn` files (recursed),
  - the literal word `clipboard` to read PGN from the system clipboard
    (probes `wl-paste`, `xclip`, `xsel` in that order).
- `--db PATH` — database directory; created if missing. Default: `tictac_db`.

The importer reports per-source progress and a final total. Per-game listings
are intentionally omitted on import — game details are only printed by the
`search` commands. Use `tictac search id --id N` to look up an individual
game after ingest.

```
tictac import clipboard --db mydb
# Importing clipboard (417 bytes):
#   -> 3 games
# Compacting position index... done
# Saving sequence index... done
# Total: 3 games imported, 3 games in database
```

The importer is incremental for **file** and **directory** inputs: re-importing
the same file is a no-op. The manifest file (`<db>/manifest.txt`) records
which files have already been ingested. **Clipboard imports bypass the
manifest** — re-pasting the same PGN re-imports it (with new GameIds). After
import, the position index is compacted and the sequence trie is flushed to
disk.

### `search position` — find games at a board state

```
tictac search position <fen> [--db PATH] [--limit N]
```

- `<fen>` — full FEN string (six fields: pieces, side, castling, en-passant, halfmove, fullmove). Quote it.
- `--limit N` — max results returned. Default: `20`.

Output lists `Game #<id> (ply <n>)` with the white/black names, event, and date when available. Results are verified by replaying the game from move 1 to guard against Zobrist collisions.

### `search opening` — find games whose opening matches a SAN prefix

```
tictac search opening <move> [<move> ...] [--db PATH] [--limit N]
```

Moves are **Standard Algebraic Notation**, one per CLI argument:

- Pawn moves use the destination square: `e4`, `d5`.
- Pieces are uppercase: `N` knight, `B` bishop, `R` rook, `Q` queen, `K` king.
- Captures use `x`: `Nxe5`, `exd5`.
- Castling is `O-O` (kingside) or `O-O-O` (queenside).
- Disambiguation by file or rank: `Nbd7`, `R1e2`.
- Promotions: `e8=Q`. Check `+`, mate `#`.

There is **no built-in ECO-code lookup.** `B50`, `C20`, etc. are not understood — pass the literal moves of the variation instead.

Examples:

```sh
# Open Game (ECO C20)
tictac search opening e4 e5

# Sicilian Defense, Lasker / Old Sicilian (ECO B50)
tictac search opening e4 c5 Nf3 d6

# Ruy Lopez (ECO C60+)
tictac search opening e4 e5 Nf3 Nc6 Bb5

# Queen's Gambit (ECO D06)
tictac search opening d4 d5 c4

# King's Indian setup
tictac search opening d4 Nf6 c4 g6
```

The output prefixes the listing with the **total** number of matching games and how many are shown:

```
999484 game(s) total, showing 20:

  Game #18: smach vs Cadu_Brondi [Rated Classical game]
  ...
```

Invalid SAN exits with a non-zero status and a message to stderr — the process no longer aborts.

### `--plugin` — Lua filter

Both `search position` and `search opening` accept a `--plugin <path>.lua` flag.
The script is loaded once and called for every candidate match. Returning a
truthy value keeps the result; falsy drops it. `--limit` then counts only the
accepted results, so a restrictive filter can scan more candidates than `--limit`
suggests.

```sh
tictac search opening e4 e5 Nf3 Nc6 Bb5 \
  --plugin examples/plugins/elite.lua \
  --limit 10
```

#### Plugin API

The script must define a global function `on_match(game)`:

```lua
function on_match(game)
    -- game.id          number    -- internal GameId
    -- game.white       string
    -- game.black       string
    -- game.event       string
    -- game.date        string
    -- game.white_elo   number
    -- game.black_elo   number
    -- game.result      string    -- "win" | "lose" | "draw" | "*"
    -- game.move_count  number    -- total half-moves (plies)
    -- game.ply         number?   -- only set for `search position`
    --
    -- game:moves()  -- stateful iterator over every half-move:
    --                  for i, san, uci in game:moves() do ... end
    --   i   1-based half-move index
    --   san standard algebraic notation in this game's context
    --   uci long algebraic ("e2e4", "e7e8q", ...)
    --
    -- game:fen([ply]) -> string
    --   FEN snapshot of the board after `ply` half-moves.
    --   ply omitted: defaults to game.ply if set, else 0 (starting position).
    --   ply = 0:               starting position
    --   ply = game.move_count: final position
    return true   -- keep this game in the result set
end
```

- `result` is `"win"` if White won, `"lose"` if Black won (mirroring the
  underlying chess library's enum), `"draw"`, or `"*"` for unknown.
- `game:moves()` walks moves lazily by replaying from the starting position;
  iterating to completion costs O(plies). Multiple calls on the same `game`
  return independent iterators.
- `game:fen(ply)` replays from the starting position up to `ply` and returns
  the resulting FEN. Useful with `tictac.engine.analyze{ fen = ... }` to
  evaluate any specific point of the game.
- The plugin is free to write to stdout / stderr (`io.write`, `print`),
  e.g. to produce custom output beyond the default summary line.
- Plugin errors and unhandled Lua exceptions abort the search with a stderr
  message and a non-zero exit status.

Sample plugins live under `examples/plugins/`:

- `elite.lua` — keep games where both sides are >= 2400.
- `short_decisive.lua` — print custom info for every short, decisive game and
  iterate the opening moves with `game:moves()`.
- `engine_eval.lua` — evaluate the final position with a UCI engine and keep
  balanced endings (requires `--engine`).
- `puzzles.lua` — mine tactical puzzles by scanning every ply for an eval
  swing followed by an "only move"; prints the FEN of each puzzle position
  (requires `--engine`).
- `viz_browser.lua` — feeds every accepted game's final FEN into the Qt
  board browser so you can step through results visually with prev/next
  buttons (requires `--viz`).

```sh
./build/src/tictac search opening e4 c5 \
  --plugin examples/puzzles.lua \
  --engine /usr/games/stockfish \
  --engine-option Threads=1 --engine-option Hash=128 \
  --limit 20
```

The plugin runs a cheap single-PV scan first and only re-evaluates the
flagged plies at higher depth with MultiPV, so a broad opening prefix is
viable. `--limit 20` caps total puzzles emitted (one per accepted game).

### `--engine` — UCI engine integration

`search position` and `search opening` accept `--engine <path>` to start a
UCI engine subprocess (Stockfish or any UCI-compatible binary). UCI options
are configured with `--engine-option NAME=VALUE` (repeatable). The engine is
exposed to the loaded Lua plugin as the global `tictac.engine` table.

```sh
tictac search opening e4 c5 \
  --plugin examples/plugins/engine_eval.lua \
  --engine /usr/games/stockfish \
  --engine-option Threads=1 \
  --engine-option Hash=64 \
  --limit 5
```

#### Engine API (Lua)

```lua
-- Analyze a position. Either opts.fen or opts.moves must be provided;
-- opts.moves is a list of UCI moves applied from the starting position.
local res = tictac.engine.analyze({
    fen      = "...",        -- optional; full FEN
    moves    = { "e2e4", "e7e5", ... },  -- optional; UCI moves from startpos
    depth    = 12,           -- optional; UCI `go depth` (default 10 if no movetime)
    movetime = 250,          -- optional; UCI `go movetime` in ms
    multipv  = 1,            -- optional; UCI MultiPV (default 1)
})

-- res.lines = { line, line, ... }  -- one entry per multipv slot, sorted by index
-- each line:
--   line.score    cp value (centipawns, from side-to-move's POV) | absent if mate
--   line.mate     mate-in-N (positive = mate FOR side to move)   | absent if cp
--   line.depth    nominal search depth
--   line.seldepth selective max depth
--   line.nodes    nodes searched for this line
--   line.pv       table of UCI moves
--
-- Convenience: res.score / res.mate / res.depth / res.pv mirror res.lines[1].
-- res.time_ms wall time consumed by analyze().
-- res.nodes total nodes across all PVs.

-- Set a UCI option at any point.
tictac.engine.set_option("Hash", "256")
```

Notes:

- The engine is started once at search startup and shut down after the run.
  Each `analyze()` call is **synchronous and blocking** — `position` + `go`
  are sent and the function returns when `bestmove` arrives. Keep `depth` /
  `movetime` modest if your filter scans many candidates.
- UCI scores are reported **from the side-to-move's perspective**, not
  white's, per UCI convention. Negate when the side to move is Black if you
  want a white-relative number.
- `tictac.engine` is only registered when `--engine` is supplied. Plugins
  that need the engine should bail early, e.g. `if not tictac or not
  tictac.engine then error("requires --engine") end`.
- The plugin host catches exceptions from the engine (dead subprocess,
  parse errors) and surfaces them as Lua errors that abort the search with
  exit status 2.

### `--viz` — Qt board browser

`search position` and `search opening` accept `--viz` to open a Qt window
**after** the search completes. The window has a chess board on the right
(Merida pieces) and a free-form info panel on the left. Three buttons
drive navigation:

- **Prev / Next** step through plies *inside* the current game.
- **Next Game** advances to the following entry (resets to ply 0).

`Left` / `Right` arrows mirror Prev / Next; `PgDn` mirrors Next Game.

```sh
tictac search opening e4 c5 \
  --plugin examples/viz_browser.lua \
  --viz \
  --limit 10
```

#### Visualization API (Lua)

```lua
-- New form: queue a full game so Prev / Next can step through plies.
tictac.viz.add({
    fen   = "...",                     -- optional starting FEN; default = startpos
    moves = { "e2e4", "e7e5", ... },   -- UCI moves applied from the starting FEN
    info  = {                          -- key/value pairs shown on the left panel
        white      = game.white,
        black      = game.black,
        white_elo  = tostring(game.white_elo),
        result     = game.result,
    },
})

-- Back-compat form: a single static position with no ply navigation.
tictac.viz.add(fen, info_table)

-- Number of entries currently buffered.
local n = tictac.viz.count()
```

Notes:

- `tictac.viz` is only registered when `--viz` is supplied. Plugins that
  rely on it should bail early, e.g. `if not tictac or not tictac.viz then
  error("requires --viz") end`.
- The browser runs **after** the search loop finishes — entries are
  buffered, not streamed.
- A move that fails to parse / apply truncates that entry's ply list at
  the failure point; subsequent moves are dropped silently.
- Build-time: visualization is compiled when CMake finds Qt6 or Qt5
  **Widgets + Svg** at configure time (`TICTAC_BUILD_VIZ=ON` by default;
  pass `-DTICTAC_BUILD_VIZ=OFF` to skip the dependency entirely). The
  Merida piece SVGs are bundled and licensed under GPL-2.0-or-later — see
  `assets/pieces/merida/COPYING.txt`.

### `stats` — summarize a database

```
tictac stats [--db PATH]
```

Prints the database path, total games, and total position-index entries.

### `compact` — compact the position index

```
tictac compact [--db PATH]
```

Rewrites the position index in compacted form. `import` runs this automatically at the end of every run; the standalone subcommand is for ad-hoc maintenance.

## Database layout

A tictac database is a directory. After `import` it contains:

```
<db>/
  manifest.txt        # list of ingested PGN files (sha + path)
  games.dat           # packed game records (header + compact moves)
  games.idx           # offset table indexed by GameId
  pos_index/          # Zobrist-hash position index (sharded)
  sequence_trie.sqlite # SQLite DB backing the opening trie, first 30 plies
```

The trie depth (30 plies = 15 full moves) is fixed at construction. Searching with more plies than that returns no results.

## Testing

Tests use Catch2 (fetched via CMake) and require the debug build:

```sh
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
ctest --test-dir build-debug --output-on-failure
```

Run the test binary directly to filter by tag/name:

```sh
./build-debug/tests/tictac_tests              # all tests
./build-debug/tests/tictac_tests "[search]"   # by tag
./build-debug/tests/tictac_tests "[cli]"      # CLI integration tests only
./build-debug/tests/tictac_tests "search opening rejects non-SAN tokens with stderr message"
```

Useful tags:

| Tag         | What it covers                                               |
| ----------- | ------------------------------------------------------------ |
| `[cli]`     | End-to-end tests that drive `CliApp::run()` per subcommand   |
| `[import]`  | `import` paths (file / directory / clipboard / re-import)    |
| `[search]`  | `search position` / `search opening` (engine + CLI)          |
| `[lua]`     | `--plugin` Lua integration                                   |
| `[engine]`  | `--engine` UCI integration                                   |
| `[clipboard]` | `import clipboard` (skipped when no clipboard tool found)  |
| `[viz]`     | FEN board model + `VizSession` lifecycle (offscreen Qt)      |

The CLI suite drives the in-process binary and captures stdout/stderr at the
file-descriptor level, so it covers both C++ (`std::cout`) and C (Lua's
`io.write`) output paths. Tests that depend on external tools skip cleanly:

- `[clipboard]` tests skip when `xclip` is missing or `DISPLAY` is unset.
- `[engine]` tests skip when `stockfish` is not on `PATH`.

Fixtures live under `tests/fixtures/`:

- `simple_game.pgn`, `multi_game.pgn` — PGN inputs.
- `filter_alpha.lua`, `move_iter.lua`, `fen_iter.lua`, `no_on_match.lua`,
  `engine_label.lua` — Lua scripts exercised by the `[lua]` and `[engine]`
  tests.

## Source layout

```
src/
  cli/        Command-line frontend (cli_app.{hpp,cpp})
  core/       Shared types, game record
  import/     PGN parsing pipeline + visitor
  storage/    game_store, position_index, sequence_index,
              mmap_file, db_manifest
  search/     SearchEngine (position + opening queries)
  plugin/     LuaPlugin -- loads .lua filters for `search --plugin`
  engine/     EngineInterface + UciEngine -- UCI subprocess wrapper
  viz/        Qt board browser (BoardModel + BoardWidget +
              BoardWindow + VizSession); compiled when Qt is found
  main.cpp    Thin entry point — instantiates CliApp
tests/        Catch2 tests + PGN fixtures
bench/        Benchmarks (off by default)
cmake/        Compiler warning preset + FetchContent setup
```

## Troubleshooting

- **`Invalid opening moves: Failed to parse san. ...`** — you passed something that is not SAN (commonly an ECO code like `B50`). See the opening-search section for the right format.
- **`No games found with this opening.`** — the prefix is valid SAN but no imported game matches. Try a shorter prefix or check `tictac stats` to confirm the database is populated.
- **Empty results from `search position`** — verify the FEN is well-formed and quoted; the parser is strict about all six fields. A position past move 30 of the game will not be in the trie but should still be in the position index.
- **First configure fails fetching dependencies** — `FetchContent` needs network access. Re-run `cmake -B build` once connectivity is restored; partial state under `build/_deps/` is safe to delete.

## License

GPL-3.0. See `LICENSE`.
