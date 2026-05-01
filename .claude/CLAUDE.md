# CLAUDE.md

## Project

Chess PGN engine/parser. Parses, searches, and filters chess games in PGN format based on user-defined criteria.

## Build

CMake project. Two build directories:

- **Release:** `build/`
- **Debug:** `build-debug/`

```sh
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

Always use the debug build when diagnosing issues or running tests. Use release for final/performance runs.

## Commits

Before commiting, always make sure that you've updated the tests, the examples and the README to reflect the latest changes. There's no need to actually run the tests. But remember to modify the unit tests to reflect the code you've just created.

Don't use the `Co-Authored-By` tag; instead, use `Assisted-By: <MODEL>`, where `<MODEL>` is the full version of the current model used to generate the code.
