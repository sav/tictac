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
  they arrive — stream game-by-game rather than buffering many games and waiting for
  EOF.

- **Streaming parallel pipeline.** Read the file game-by-game without mapping large
  chunks into memory: read one game, dispatch it to a worker thread for the plugin
  pipeline, then read the next. Synchronize output so two or more threads never write
  simultaneously, which would corrupt the format.
