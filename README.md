# tictac

Chess PGN database analyzer. Imports PGN files into an on-disk store, then answers two kinds of queries fast:

- **Position search** — given a FEN, find games that reach that exact board state.
- **Opening search** — given a sequence of SAN moves, find games whose opening matches that prefix.

It is a single C++20 binary with a CLI11 frontend. State lives entirely in a directory of mmap-backed files; there is no daemon, server, or external database.

## Status

Pre-1.0. The on-disk format is not committed-to; rebuild the database after pulling. Tested on Linux. Imports are currently single-threaded regardless of `--threads` (the flag is reserved for the parallel import path that is not yet wired up).

## Requirements

- C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.20+
- Internet access on the first configure (CMake `FetchContent` pulls the chess library, CLI11, and Catch2)

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

| Option                       | Default | Description                       |
| ---------------------------- | ------- | --------------------------------- |
| `TICTAC_BUILD_TESTS`         | `ON`    | Build the Catch2 test suite       |
| `TICTAC_BUILD_BENCH`         | `OFF`   | Build benchmarks (under `bench/`) |
| `TICTAC_ENABLE_SANITIZERS`   | `OFF`   | Compile with ASan + UBSan         |

Example with sanitizers:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DTICTAC_ENABLE_SANITIZERS=ON
cmake --build build-asan -j
```

## Quick start

```sh
# 1. Build (Release)
./build.sh

# 2. Import a PGN file or a directory of PGN files
./build/src/tictac import path/to/games.pgn --db mydb

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

### `import` — ingest PGN

```
tictac import <input> [--db PATH] [--threads N]
```

- `<input>` — a `.pgn` file or a directory containing `.pgn` files (recursed).
- `--db PATH` — database directory; created if missing. Default: `tictac_db`.
- `--threads N` — reserved; currently ignored (see Status).

The importer is incremental: re-importing the same file is a no-op. The manifest file (`<db>/manifest.txt`) records which files have already been ingested. After import, the position index is compacted and the sequence trie is flushed to disk.

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

  Game #18: Milo9988 vs marselle116 [Rated Classical game]
  ...
```

Invalid SAN exits with a non-zero status and a message to stderr — the process no longer aborts.

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
  sequence_trie.dat   # opening trie, first 30 plies
```

The trie depth (30 plies = 15 full moves) is fixed at construction. Searching with more plies than that returns no results.

## Testing

Tests use Catch2 (fetched via CMake) and require the debug build:

```sh
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
ctest --test-dir build-debug --output-on-failure
```

Or run the test binary directly to filter by tag/name:

```sh
./build-debug/tests/tictac_tests
./build-debug/tests/tictac_tests "[search]"
```

Fixtures live under `tests/fixtures/` (`simple_game.pgn`, `multi_game.pgn`).

## Source layout

```
src/
  cli/        CLI11 frontend (cli_app.{hpp,cpp})
  core/       Shared types, game record
  import/     PGN parsing pipeline + visitor
  storage/    game_store, position_index, sequence_index,
              mmap_file, db_manifest
  search/     SearchEngine (position + opening queries)
  engine/     Engine analysis interface (placeholder)
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
