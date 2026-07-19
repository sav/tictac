#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# Configure and build tictac under build/.

set -e

if [ "$1" = "-c" ]; then
    shift
    rm -rf build
fi

cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ "$@"
cmake --build build -j $(nproc) --verbose
