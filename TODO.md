# TODO

Deferred work and reserved interfaces that are not yet implemented. Keep this current
so nothing gets silently lost.

## Runtime

- **Parallel game workers (`-j`/`--jobs`).** The flag is accepted and stored in
  `opts.jobs`, but nothing consumes it — `Runtime::run` processes games sequentially.
  The CLI help marks it "Reserved". Implement a parallel execution path (thread pool /
  `std::execution` policy) that honors the requested worker count, then drop the
  "Reserved" note from the help text.

- **Read PGNs from stdin.** Accept PGN input on standard input, and process games as
  they arrive — stream game-by-game rather than buffering many games and waiting for
  EOF.

- **Streaming parallel pipeline.** Read the file game-by-game without mapping large
  chunks into memory: read one game, dispatch it to a worker thread for the plugin
  pipeline, then read the next. Synchronize output so two or more threads never write
  simultaneously, which would corrupt the format.
