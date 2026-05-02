#!/usr/bin/env bash
# Install all build/runtime dependencies for tictac on Ubuntu / Debian.
#
# Usage:
#   ./config.sh                # install required + all optional
#   ./config.sh --no-viz       # skip Qt (no --viz support)
#   ./config.sh --no-engine    # skip Stockfish (no --engine support)
#   ./config.sh --no-clipboard # skip xclip / wl-clipboard
#   ./config.sh --qt6          # use Qt6 instead of Qt5 for --viz
#
# After this script completes, run ./build.sh (and optionally
# ./build.sh -debug) to compile.

set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "error: apt-get not found — this script supports Ubuntu / Debian only." >&2
    echo "See README.md for Fedora and other distributions." >&2
    exit 1
fi

want_viz=1
want_engine=1
want_clipboard=1
qt_major=5

for arg in "$@"; do
    case "$arg" in
        --no-viz)       want_viz=0 ;;
        --no-engine)    want_engine=0 ;;
        --no-clipboard) want_clipboard=0 ;;
        --qt6)          qt_major=6 ;;
        --qt5)          qt_major=5 ;;
        -h|--help)
            sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown option: $arg" >&2
            echo "run with --help for usage." >&2
            exit 1
            ;;
    esac
done

pkgs=(build-essential cmake git libsqlite3-dev)

if [[ $want_viz -eq 1 ]]; then
    if [[ $qt_major -eq 6 ]]; then
        pkgs+=(qt6-base-dev qt6-svg-dev)
    else
        pkgs+=(qtbase5-dev libqt5svg5-dev)
    fi
fi

if [[ $want_engine -eq 1 ]]; then
    pkgs+=(stockfish)
fi

if [[ $want_clipboard -eq 1 ]]; then
    pkgs+=(xclip wl-clipboard)
fi

echo "tictac will install:"
printf '  %s\n' "${pkgs[@]}"
echo

sudo apt-get update
sudo apt-get install -y "${pkgs[@]}"

cat <<EOF

Done. Next steps:
  ./build.sh           # release build at build/src/tictac
  ./build.sh -debug    # debug build at build-debug/src/tictac
EOF
