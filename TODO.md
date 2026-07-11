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
