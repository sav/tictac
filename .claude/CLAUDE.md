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
