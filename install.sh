#!/usr/bin/env bash
# Install (or uninstall) the tictac binary system-wide.
#
# Usage:
#   ./install.sh                  # install build/src/tictac to /usr/local/bin
#   ./install.sh --uninstall      # remove a previously installed copy
#   ./install.sh --prefix PATH    # custom prefix (default: /usr/local)
#
# Requires that ./build.sh has produced build/src/tictac. Use ./config.sh
# first if you still need to install the build dependencies.
#
# Files placed:
#   <prefix>/bin/tictac
#   <prefix>/share/tictac/examples/*.lua
# Uninstall removes both.

set -euo pipefail

prefix=/usr/local
mode=install

while [[ $# -gt 0 ]]; do
    case "$1" in
        --uninstall)   mode=uninstall ;;
        --prefix)      prefix="$2"; shift ;;
        --prefix=*)    prefix="${1#--prefix=}" ;;
        -h|--help)
            sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            echo "run with --help for usage." >&2
            exit 1
            ;;
    esac
    shift
done

bin_dst="$prefix/bin/tictac"
share_dst="$prefix/share/tictac"

# Use sudo only when we can't write to the destination ourselves (typical
# /usr/local case). Custom prefixes under $HOME work without elevation.
needs_sudo() {
    local target="$1"
    while [[ ! -e "$target" ]]; do target="$(dirname "$target")"; done
    [[ ! -w "$target" ]]
}

sudo_cmd=
if needs_sudo "$prefix"; then
    sudo_cmd=sudo
fi

if [[ "$mode" == "uninstall" ]]; then
    echo "removing:"
    echo "  $bin_dst"
    echo "  $share_dst/"
    $sudo_cmd rm -f "$bin_dst"
    $sudo_cmd rm -rf "$share_dst"
    echo "done."
    exit 0
fi

root="$(cd "$(dirname "$0")" && pwd)"
bin_src="$root/build/src/tictac"
if [[ ! -x "$bin_src" ]]; then
    echo "error: $bin_src not found." >&2
    echo "run ./build.sh first to produce a release binary." >&2
    exit 1
fi

echo "installing to $prefix:"
echo "  $bin_dst"
echo "  $share_dst/examples/"
echo

$sudo_cmd install -Dm755 "$bin_src" "$bin_dst"
$sudo_cmd install -d "$share_dst/examples"
$sudo_cmd install -m644 "$root/examples"/*.lua "$share_dst/examples/"

echo
echo "done. tictac is on your PATH if $prefix/bin is."
echo "uninstall later with: $0 --uninstall${prefix:+ --prefix $prefix}"
