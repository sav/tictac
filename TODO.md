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

- **Live cross-worker shared state (`ctx.global`).** The finish-time merge hook is
  done: `partial`/`reduce` fold every worker's per-worker `ctx.scope` into one
  report (the runtime lifts each partial out of its Lua state as deep-copied plain
  data and calls `reduce` once). That covers aggregation emitted *at the end*. It
  does not cover a plugin that needs state shared *during* `process` -- a running
  dedup set, a cross-worker counter read back mid-run. For that, add a `ctx.global`
  table owned by the runtime with serialized access and a compare-and-set (plain
  read-modify-write over a synchronized table still races). This is what the removed
  `ctx.shared` gestured at without being safe for it.

- **Skip the partial transport at `-j1`.** When there is a single pipeline, `reduce`
  still round-trips the whole partial out of and back into the same Lua state via the
  C++ `ValueTree`. It is correct and one-time, but for a large aggregate (cheat's tens
  of thousands of move records) it is pure overhead. When `jobs == 1`, hand the live
  partial straight to `reduce` without the conversion.

- **Leveled logging with `-v`/`-q`.** The runtime never says what it is doing: nothing
  reports how many workers `-j` resolved to, which plugins loaded and in what order, which
  file is being parsed, or which hook is running on which worker. Add a leveled logger
  writing to stderr, with a repeatable `-v` raising the level (info, debug, trace), a `-q`
  dropping it to errors only, and the two rejected together. Both short flags are free --
  `--version` has no `-v` form. `ctx.log` already prints `[plugin:index] level: msg` at
  four levels but is ungated, so the same threshold should gate it and the runtime's own
  lines should match its format. Everything goes to stderr: stdout carries the games under
  the default `--output -`.

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

- **`fork`+`execlp` runs from worker threads now.** The shared `EnginePool`
  creates engines lazily on the first borrow, which happens on a worker thread,
  not on the single-threaded startup path as before. Creation is serialized by
  the pool's `create_mu_` so no two forks overlap, but `execlp` does a PATH
  search that can `malloc`, and `malloc` after `fork` in a multithreaded program
  is only safe when no other thread held the allocator lock at fork time. The
  window is tiny and has not misbehaved, but to remove the hazard entirely,
  resolve the engine path once up front and `execv` an absolute path (no
  PATH-search allocation), or pre-create the engines on the main thread.

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

- **Verify `Engine::policy` against a live lc0.** `policy()` and the `info string
  <move> ... (P: NN.NN%)` parser in `parsePolicy` were written to lc0's documented
  VerboseMoveStats format but have not been run against an actual lc0 build (none was
  available when added). Confirm the field layout across lc0 versions -- especially that
  the move is the first `info string` token and that no summary line slips through
  `isUciMove` -- and that `go nodes 1` yields the raw policy head. Add a round-trip test
  with a canned lc0 transcript.

## Plugins

- **`setup`/`teardown` hooks that run once for the whole run.** Under `-j N` both `init`
  and `finish` run once *per worker*, so neither can emit a one-off header or footer on a
  shared `ctx.open` writer: nothing orders one worker's `init` against another's first
  `process`, which is why `csv.lua` no longer writes a header row. Add an optional
  `setup(ctx)` called exactly once before any pipeline's `init`, and a matching
  `teardown(ctx)` after every `finish`. Both run on the main thread with no worker in
  flight, so they need no synchronization, and a writer `setup` opens reaches the
  pipelines through the existing `WriterRegistry`. Open question: whether they get worker
  1's `ctx` or a dedicated one whose `ctx.scope` no worker can see. When this lands, move
  `csv.lua`'s header row back, into `setup`, and restore it in `tests/expected/ex_csv.csv`.

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

- **`fetch.lua new` misses games sharing the cutoff second.** Incremental fetch takes
  the newest game already on disk as the cutoff and keeps only downloaded games with a
  strictly greater timestamp. PGN timestamps are second-granular, so a different game
  played in the very same second as the cutoff game is never picked up. It is a tolerable
  gap (two of one player's games starting in the same second is near-impossible) and the
  by-design cost of reading only the first and last existing games. Closing it would mean
  recording the cutoff game's id (Site/Link URL) and keeping same-second games whose id
  differs -- a small amount of extra bookkeeping if it ever matters.

- **`assess.lua` is Linux-only and re-invokes the binary.** The assessor drives the two
  shipped plugins by spawning the running tictac once per stage per user, which it finds
  through `/proc/$PPID/exe` (the binary) and `/proc/self/cmdline` (its own plugin dir,
  since the Lua sandbox omits the `debug` library). Both are Linux-specific; on another OS
  `bin=` must be passed and the plugin dir falls back to `plugins/`. It would be cleaner to
  expose the plugin's own resolved path and sibling directory through `ctx` (e.g.
  `ctx.plugin.dir`) so no `/proc` reads are needed, and/or to let a plugin feed games into
  its own pipeline so assess need not spawn a second process at all. Also: plugin-spec
  values are space-separated, so an `outdir`/path or username containing a space breaks the
  forwarded spec -- validate or quote-escape per-value if that ever matters.

## Build

- **Drop the CMake policy shim once upstream Lua bumps its minimum.** `CMakeLists.txt`
  forces `CMAKE_POLICY_VERSION_MINIMUM` to 3.5 around the walterschell/Lua sub-build
  because its CMakeLists predates CMake 3.5. Check whether a newer walterschell/Lua tag
  raises its own `cmake_minimum_required`, and if so bump the `v5.4.5` pin and drop the
  save/force/restore shim entirely.

## cheat.lua screener

`plugins/cheat.lua` ships several engine-correlation indicators folded into one
posterior, plus a within-game localized-assistance scan and an optional Maia
human-difficulty oracle (`maia=`). These extensions were deliberately left out
and are worth adding:

- **Per-player Maia banding.** The Maia oracle loads one network, banded to the
  first target move's Elo, and reuses it for the whole run. The old blocker is gone:
  `ctx.engine` now returns a handle keyed by path+options, so two bands are two
  distinct pool signatures (different `WeightsFile`) rather than a collision. What
  remains is the cheat.lua side: pick the band nearest *each* player's Elo and keep a
  per-band handle, instead of banding once to the first target move.
- **Maia oracle is unvalidated end-to-end.** The `z_maia` indicator was validated
  offline against the `maia2` Python model, not against the shipped `lc0`+Maia UCI path
  (which depends on `Engine::policy`, itself untested -- see Engine). Once lc0 is wired,
  confirm `z_maia` matches the offline computation on a known game.
- **Multi-depth agreement.** Analyse each position at a shallow and a deep depth and
  test whether the player's moves track the deep-only best more than an honest player
  would -- a signature of a strong engine specifically, not just good play. Doubles the
  engine cost, so gate it behind an argument.
- **Clock-model signal beyond correlation.** The move-time indicator is only a
  time-vs-difficulty correlation. Add an explicit per-move time model (expected think
  time vs observed), flagging hard moves played near-instantly and suspiciously flat
  time distributions.
- **Posterior calibration from labelled data (partly done; needs scaling).** `beta` was
  bumped from 0.9 to 1.3 after a first pass against a labelled chess.com sample: 37 banned
  (`status: closed:fair_play_violations` via the public API) + 41 clean accounts, screened
  blind. Findings: the composite only discriminates modestly (pooled AUC ~0.70, held-out
  0.62), but its high-posterior tail is clean -- at `beta=1.3` the SUSPICIOUS band fires on
  the 5 clearest banned accounts (all combined-z >= 3) with 0/41 clean false positives,
  whereas at the old 0.9 it fired on essentially nobody. Caveats that must be fixed before
  trusting this: (a) the sample is small and biased (opponents of a handful of known
  accounts), so "zero FP" is sample-specific; (b) it is balanced (~1:1) while real
  prevalence is ~4%, so the apparent precision will not hold at deployment base rate; (c)
  the subtle/selective cheaters (AD0420, jongGun01, ShuvalovAndrii) stay below threshold --
  this screen catches ~20% of bans, not the hard tail. Next: harvest a larger, less biased
  labelled set, fit the slope properly (logistic regression of label on combined-z) and set
  the `prior` to the real base rate, hold out a test split, and report sensitivity at a
  fixed low false-positive rate rather than tuning the threshold to the sample.
- **Overdispersion SDs are uncalibrated defaults.** The indicator z-scores now
  standardize against an overdispersed null (binomial within-player variance plus a
  between-player term), which is what stops the z from growing like `sqrt(n)` and tagging
  every player with enough moves. But the between-player SDs (`disp_rate`, `disp_acpl`,
  `disp_exc`, `disp_elo`) are hardcoded round-number defaults, not measured. They should
  be estimated per rating band from a reference population of honest players -- extend
  `field_calibration` (which today fits only `tau`) to also fit each indicator's mean and
  between-player SD, and make the `reference` path the default source when enough field
  moves are present.
- **Segment scan still uses binomial variance.** The localized-assistance scan
  (`scan_segment`) standardizes each window against binomial variance only, with no
  overdispersion term. It is bounded in length (`seg_max`) and Bonferroni-corrected over
  the windows examined, so it does not blow up with `n` the way the global indicators did,
  but a lucky honest stretch can still over-flag. Fold a between-player term into the
  window z once the dispersion SDs above are calibrated.
