# CLAUDE.md

## Code Style

- Always use C++23.
- Avoid unnecessary comments.

## File Headers

- Every source file (C++, Lua, CMake, shell, etc.) must start with a header:
  an SPDX license line, a copyright line, and a one-line description of what the
  file is. Use the file's native comment syntax and keep a shebang, if any, on
  the first line with the header directly below it.

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
  //
  // <one-line description of the file>
  ```

- New files get this header at creation. Skip generated files, plain data
  (e.g. `VERSION`, PGN samples), and files with no comment syntax.

## Building

- Don't build after every change. Build only when explicitly asked. When you do build, fix any errors that come up.

## Best Practices

- For development instructions, follow @DEV.md
- For git and github best practices, follow @GIT.md
- Use the clangd LSP tools for C++ navigation.

## Documentation

- Keep `README.md` in sync with the code. Whenever a change affects something the
  README describes — CLI flags, build steps, dependencies, the plugin model, usage
  examples, or the project's scope — update the README in the same change.

## Project Tracking

- Whenever something is deferred, stubbed, or left as a reserved/unimplemented interface
  (e.g. an accepted CLI flag that nothing consumes yet), record it in `TODO.md` at the
  repo root. Don't rely on code comments alone — it's too easy to lose track of what
  still needs implementing.

## Git

- Never use the `Co-Authored-By` tag in commits. Use `Assisted-By: <Model>` instead.
- Name branches as `claude/<branch>`, typically after the feature.
- After addressing review comments, commit the changes as a fixup and push to the remote branch.

## Working Together

- A question is not a request to change code. I often just discuss the code without wanting edits. I'll explicitly ask when I want a change; when in doubt, ask before touching anything.
- When addressing PR review comments, don't accept them blindly. If a comment is a question, answer it and justify your decision rather than immediately changing code. Change it only if you agree, or if the reviewer explicitly tells you to after your reply. The reviewer may raise several questions across a thread, so if another one comes up, keep making your case rather than caving.

