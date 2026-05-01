#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
usage: $(basename "$0") [-n|--dry-run] [--db] [-h|--help]

Removes build artifacts and downloaded dependencies from the repo root.

Default targets (always cleaned):
    build/                  Release build tree (CMake + FetchContent _deps)
    build-debug/            Debug build tree
    CMakeFiles/             Stray CMake bookkeeping at repo root
    CMakeCache.txt          Stray CMake cache at repo root
    cmake_install.cmake     Stray CMake install script at repo root
    Makefile                Stray top-level Makefile from an in-source configure
    compile_commands.json   Compile DB symlink/file at repo root
    split                   Compiled split binary (split.cpp is preserved)
    *.dat *.idx             Loose index/data shards at repo root

Opt-in:
    --db        Also wipe tictac_db/ (the local game database).
                This is user data, not a build artifact, so it is never
                removed unless you pass this flag.

Options:
    -n, --dry-run   Print what would be removed; do not delete anything.
    -h, --help      Show this message.
EOF
}

dry_run=0
wipe_db=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n|--dry-run) dry_run=1; shift ;;
        --db)         wipe_db=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

root="$(cd "$(dirname "$0")" && pwd)"
cd "$root"

targets=(
    build
    build-debug
    CMakeFiles
    CMakeCache.txt
    cmake_install.cmake
    Makefile
    compile_commands.json
    split
)

# Collect loose data/index shards at repo root only (not recursively),
# avoiding files tracked under src/, tests/, examples/, etc.
shopt -s nullglob
for f in ./*.dat ./*.idx; do
    targets+=("${f#./}")
done
shopt -u nullglob

if [[ $wipe_db -eq 1 ]]; then
    targets+=(tictac_db)
fi

removed_any=0
for t in "${targets[@]}"; do
    if [[ -e "$t" || -L "$t" ]]; then
        size="$(du -sh "$t" 2>/dev/null | awk '{print $1}')"
        if [[ $dry_run -eq 1 ]]; then
            printf 'would remove  %-24s (%s)\n' "$t" "${size:-?}"
        else
            printf 'removing      %-24s (%s)\n' "$t" "${size:-?}"
            rm -rf -- "$t"
        fi
        removed_any=1
    fi
done

if [[ $removed_any -eq 0 ]]; then
    echo "nothing to clean."
fi
