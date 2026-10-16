#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# Configure and build tictac under build/.
#
# -c reconfigures from scratch; anything else is passed on to cmake. To build
# with a sanitizer, pass -DTICTAC_SANITIZER=thread (or address, undefined);
# ThreadSanitizer is the one worth running against --jobs. It needs the
# compiler's sanitizer runtime, which on Debian/Ubuntu is
# libclang-rt-<version>-dev:
#
#   ./build.sh -c -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTICTAC_SANITIZER=thread

set -e

if [ "$1" = "-c" ]; then
    shift
    rm -rf build
fi

cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ "$@"
cmake --build build -j $(nproc) --verbose
