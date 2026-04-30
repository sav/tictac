#!/usr/bin/env bash
set -euo pipefail

mode="${1:--release}"

case "$mode" in
    -release)
        build_type=Release
        build_dir=build
        ;;
    -debug)
        build_type=Debug
        build_dir=build-debug
        ;;
    *)
        echo "usage: $0 [-release|-debug]" >&2
        exit 1
        ;;
esac

root="$(cd "$(dirname "$0")" && pwd)"

cmake -S "$root" -B "$root/$build_dir" -DCMAKE_BUILD_TYPE="$build_type"
cmake --build "$root/$build_dir" -j
