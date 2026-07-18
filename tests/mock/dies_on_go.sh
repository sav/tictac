#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# An engine that completes the handshake and then dies on the first "go", the
# way a real engine does when it crashes mid-search. analyse() must report the
# lost process instead of hanging or taking the run down with it.

while IFS= read -r line; do
    case "$line" in
        uci)
            echo "uciok"
            ;;
        isready)
            echo "readyok"
            ;;
        go*)
            exit 1
            ;;
        quit)
            exit 0
            ;;
    esac
done
