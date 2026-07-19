# TODO

Deferred work and reserved interfaces that are not yet implemented. Keep this current
so nothing gets silently lost.

## Runtime

- **Parallel game workers (`-j`/`--jobs`).** `Runtime::run` processes games
  sequentially. Add a parallel execution path (thread pool / `std::execution` policy)
  that reads games one at a time and dispatches each to a worker, synchronizing output
  so concurrent writes never corrupt the format.

- **Read PGNs from stdin.** Accept PGN input on standard input, and process games as
  they arrive -- stream game-by-game rather than buffering many games and waiting for
  EOF.

- **Early parser abort rides on an exception.** `parseGames` stops mid-file by throwing
  `StopParsing` out of `Builder::endPgn` and catching it around `readGames`, because
  `chess::pgn::StreamParser` offers no way to be told to stop. The library has the
  plumbing next door -- `Visitor::skipPgn()`/`skip()` -- so a `stop()` honored by the
  `readGames` loop is a small upstream patch. Send it; once a release carries it, bump
  the FetchContent pin and drop the sentinel exception for a plain flag check.

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

- **Drop the CMake policy shim once upstream Lua bumps its minimum.** `CMakeLists.txt`
  forces `CMAKE_POLICY_VERSION_MINIMUM` to 3.5 around the walterschell/Lua sub-build
  because its CMakeLists predates CMake 3.5. Check whether a newer walterschell/Lua tag
  raises its own `cmake_minimum_required`, and if so bump the `v5.4.5` pin and drop the
  save/force/restore shim entirely.
