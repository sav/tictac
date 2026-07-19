# TODO

Deferred work and reserved interfaces that are not yet implemented. Keep this current
so nothing gets silently lost.

## Runtime

- **Parallel game workers (`-j`/`--jobs`).** `Runtime::run` processes games
  sequentially. The `-j`/`--jobs` flag and its `RunOptions` field were removed rather
  than left as inert CLI surface; re-add them alongside a parallel execution path
  (thread pool / `std::execution` policy) that actually honors the requested worker
  count.

- **Read PGNs from stdin.** Accept PGN input on standard input, and process games as
  they arrive -- stream game-by-game rather than buffering many games and waiting for
  EOF.

- **Streaming parallel pipeline.** Read the file game-by-game without mapping large
  chunks into memory: read one game, dispatch it to a worker thread for the plugin
  pipeline, then read the next. Synchronize output so two or more threads never write
  simultaneously, which would corrupt the format.

- **Drop callback for plugin errors.** Under `--on-error pass`/`drop`, a failing
  `process()` only writes to stderr. Let the embedder supply a "drop callback" that is
  invoked with the error (plugin name, game index, message) so failures can be
  collected or handled programmatically instead of merely printed.

- **NAGs are never parsed from input.** `Game::pgn()` writes `MoveData::nags`, and
  `move:nags()`/`move:addNag()` are documented in LUA.md, but nothing populates the
  field: `chess::pgn::Visitor::move()` only hands back the move and its comment, so a
  `$1` in the source PGN is dropped and a read-write round-trip loses every
  annotation. Parsing NAGs needs either a change to the upstream visitor interface or
  a pre-pass over the movetext. Until then `move:nags()` returns an empty table for
  every parsed game.

- **Variations (RAV) are never parsed from input.** `Game` models only the mainline:
  `moves` is a flat `std::vector<MoveData>` with no room for a subtree. The upstream
  parser does not expose variations either -- it hits `(` and calls
  `skipUntil('(', ')')`, so a variation never reaches `Visitor::move()`. The moves are
  therefore dropped silently, and `Game::pgn()` writes the game back without them:
  `1. e4 e5 (1... c5 2. Nf3) 2. Nf3` round-trips as `1. e4 e5 2. Nf3`. Supporting them
  needs both a recursive move model (each `MoveData` owning child lines) and a
  movetext pre-pass or an upstream visitor change to see the variation at all.

## Engine

- **`EINTR` tears down the engine session.** `Engine::send` and `Engine::readLine` treat
  any `n <= 0` from `::write`/`::read` as fatal. A signal delivered mid-call makes the
  syscall return -1 with `errno == EINTR`, which is retryable rather than an error, so a
  well-timed signal kills a working engine with "write failed" or "process closed
  unexpectedly". Both loops should retry on `EINTR` and only throw for real failures.

- **engine.cpp defines members in a different order than engine.hpp declares them.**
  DEV.md asks for the definition order to follow the declaration order; the header lists
  `setOption`/`analyse` before the private helpers, while the `.cpp` puts `handshake` and
  `shutdown` first. Reordering is a large, purely mechanical diff, so it was left out of
  the review round that found it. Also worth folding in: `::read`/`::write` results are
  held in `std::ptrdiff_t` rather than the POSIX `ssize_t`.

## Plugins

- **Argument schema validation and `--help`** Plugins may declare a `meta.args`
  schema (`type`/`default`/`help`) plus `meta.version`/`meta.description`, and
  LUA.md says the schema is "used for validation and --help" -- but `loadPlugins`
  only reads `meta.name`. Argument types are never validated and there is no
  per-plugin help. Implement schema-based validation and a plugin `--help`/info
  path, or drop the unimplemented promise from the docs.

- **Accessor defaults from `meta.args`.** The `ctx.args` accessors currently
  return `nil` for an absent key unless the plugin passes an explicit default at
  the call site. Once `meta.args` is read (see above), fall back to the schema's
  declared `default` for that key so `ctx.args:number("depth")` yields the
  declared default without repeating it in every accessor call.

## Build

- **CMake policy relaxation for the Lua sub-build.** `CMakeLists.txt` sets
  `CMAKE_POLICY_VERSION_MINIMUM` to 3.5 around the walterschell/Lua block because that
  project's CMakeLists predates CMake 3.5. The setting is saved and restored so it
  cannot spread to later dependencies, but it is still a workaround: check whether a
  newer walterschell/Lua tag raises its own `cmake_minimum_required`, and if so bump
  the pin from v5.4.5 and drop the save/restore entirely.
