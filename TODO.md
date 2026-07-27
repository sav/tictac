# TODO

Deferred work and reserved interfaces not yet implemented. Keep this current so
nothing gets silently lost.

## Concurrency

`-j`/`--jobs` now spreads games across a pool of workers. These two remaining pieces
are best designed together: the non-blocking engine API lets each worker keep several
analyses in flight instead of blocking per position, and readiness-based I/O
multiplexing (`poll`/`epoll`) lets a single event loop drive many engines without any
thread blocking on a single read.

- **Non-blocking engine API.** `Engine::analyse` blocks the caller until the engine
  reports `bestmove`, so one thread can drive only a single position at a time and cannot
  overlap analysis with parsing or with other engines. Add an asynchronous entry point --
  submit a position and get back a `std::future<Analysis>` (or a completion callback) that
  resolves when `bestmove` arrives -- so callers keep multiple analyses in flight and stay
  responsive. This is the API the per-ply evaluation (see Engine) and a process pool over
  a shared FEN queue would build on.

- **Blocking reads have no timeout.** `Engine::readLine` blocks on `::read` with
  no bound, so a live-but-silent engine hangs the program forever (a dead engine
  is already handled by the `n <= 0` throw). Gate reads on `::poll` with an idle
  timeout. The same readiness check generalizes to an `epoll` loop that
  multiplexes several engines' output streams for the async API above.

## Runtime

- **Cross-worker aggregation via `ctx.global`.** A Lua state cannot be shared between
  threads, so under `-j N` each worker owns a whole pipeline: `ctx.scope` is private to
  it, `finish` runs once per worker, and anything accumulated across games is per worker.
  Aggregating plugins (a histogram, a dedup table) are therefore only correct at `-j1`,
  which is why none ship under `plugins/`. Add a `ctx.global` table owned by the runtime
  rather than by any worker's Lua state, with every access serialized and values crossing
  the boundary as deep-copied plain data. A synchronized table alone cannot make
  read-modify-write atomic, so it needs a compare-and-set primitive (or a `finish`-time
  merge hook) alongside it. This is what the removed `ctx.shared` gestured at without ever
  being safe for it.

- **Read PGNs from stdin.** Accept PGN input on standard input and process games as
  they arrive.

- **Replace the exception-based parser abort with an upstream `stop()`.** Stopping
  mid-file currently unwinds a `StopParsing` exception out of `Builder::endPgn`, caught
  around `readGames`, because `chess::pgn::StreamParser` exposes no abort hook. Contribute
  a `stop()` upstream next to the existing `skipPgn()`/`skip()`, bump the FetchContent pin
  to a release that carries it, and swap the exception for a flag check in the
  `readGames` loop.

- **Drop callback for plugin errors.** Under `--on-error pass`/`drop`, a failing
  `process()` only writes to stderr. Let the embedder supply a "drop callback" invoked
  with the error (plugin name, game index, message) so failures can be collected or
  handled programmatically instead of merely printed.

- **Implement the missing PGN parsers.** Two movetext constructs are parsed away on
  input, so a read-write round-trip drops them:

  - *NAGs are never parsed.* `Game::pgn()` writes `MoveData::nags` and
    `move:nags()`/`move:addNag()` are documented in LUA.md, but nothing populates the
    field: `chess::pgn::Visitor::move()` hands back only the move and its comment, so a
    `$1` in the source PGN is dropped and a read-write round-trip loses every annotation.
    Parsing NAGs needs either a change to the upstream visitor interface or a pre-pass
    over the movetext. Until then `move:nags()` returns an empty table for every parsed
    game.

  - *Variations (RAV) are never parsed.* `Game` models only the mainline: `moves` is a
    flat `std::vector<MoveData>` with no room for a subtree. The upstream parser does not
    expose variations either -- it hits `(` and calls `skipUntil('(', ')')`, so a
    variation never reaches `Visitor::move()`. The moves are dropped silently and
    `Game::pgn()` writes the game back without them: `1. e4 e5 (1... c5 2. Nf3) 2. Nf3`
    round-trips as `1. e4 e5 2. Nf3`. Supporting them needs both a recursive move model
    (each `MoveData` owning child lines) and a movetext pre-pass or an upstream visitor
    change to see the variation at all.

## Engine

- **Share engines across workers.** `ctx.engine` memoizes per plugin instance, and `-j N`
  gives every worker its own instance, so N workers spawn N subprocesses per engine path.
  Each engine also has its own `Threads` option, so the two levels of parallelism multiply
  and oversubscribe the machine unless the user tunes them by hand. A pooled engine behind
  the non-blocking API above would let the workers share a fixed set instead.

- **`EINTR` tears down the engine session.** `Engine::send` and `Engine::readLine` treat
  any `n <= 0` from `::write`/`::read` as fatal. A signal delivered mid-call makes the
  syscall return -1 with `errno == EINTR`, which is retryable rather than an error, so a
  well-timed signal kills a working engine with "write failed" or "process closed
  unexpectedly". Both loops should retry on `EINTR` and only throw for real failures.

- **Per-ply position evaluation.** Evaluate every position with the UCI engine and surface
  its best line(s). `GameVisitor::move()` submits `board_.getFen()` per ply for
  non-blocking analysis returning a `std::future<Analysis>` (score in cp/mate plus one
  `PvLine` per MultiPV index), fulfilled when the engine reports `bestmove`; futures are
  drained and printed at end of game so parsing stays responsive. Make it opt-in and
  engine-agnostic via CLI flags: `--engine <path>`, `--engine-arg` (repeatable),
  `--engine-option NAME=VALUE` (repeatable, feeds `setoption`), `--multipv`, and the
  mutually-exclusive `--eval-depth`/`--eval-movetime`/`--eval-nodes`; without `--engine`
  behavior is unchanged. A process pool over a shared FEN queue is a natural extension.
  (Explored in the dropped PR #2 on branch `sav/old//uci-engine-adapter`, against a since
  -drifted `uci::UciEngine`/reproc++ design; re-implement over the current `Engine`.)

## Plugins

- **Argument schema validation and `--help`.** Plugins may declare a `meta.args` schema
  (`type`/`default`/`help`) plus `meta.version`/`meta.description`, and LUA.md says the
  schema is "used for validation and --help" -- but `loadPlugins` only reads `meta.name`.
  Argument types are never validated and there is no per-plugin help. Implement
  schema-based validation and a plugin `--help`/info path, or drop the unimplemented
  promise from the docs.

- **Accessor defaults from `meta.args`.** The `ctx.args` accessors return `nil` for an
  absent key unless the plugin passes an explicit default at the call site. Once
  `meta.args` is read (see above), fall back to the schema's declared `default` so
  `ctx.args:number("depth")` yields the declared default without repeating it in every
  accessor call.

## Build

- **Drop the CMake policy shim once upstream Lua bumps its minimum.** `CMakeLists.txt`
  forces `CMAKE_POLICY_VERSION_MINIMUM` to 3.5 around the walterschell/Lua sub-build
  because its CMakeLists predates CMake 3.5. Check whether a newer walterschell/Lua tag
  raises its own `cmake_minimum_required`, and if so bump the `v5.4.5` pin and drop the
  save/force/restore shim entirely.
