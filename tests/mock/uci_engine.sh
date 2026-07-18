#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# A well-behaved mock UCI engine: enough of the protocol to drive Engine end to
# end with fixed, assertable output. TT_MOCK_SCORE overrides the first line's
# score spec (e.g. "mate 3"); TT_MOCK_OPTIONS_LOG, when set, records every
# setoption the driver sends so a test can assert on the exact wire values.

multipv=1
score_spec=${TT_MOCK_SCORE:-cp 25}

pv_for() {
    case "$1" in
        1) echo "e2e4 e7e5" ;;
        2) echo "d2d4 d7d5" ;;
        *) echo "g1f3 g8f6" ;;
    esac
}

while IFS= read -r line; do
    case "$line" in
        uci)
            echo "id name mockfish 1.0"
            echo "id author tictac tests"
            echo "option name MultiPV type spin default 1 min 1 max 64"
            echo "uciok"
            ;;
        isready)
            echo "readyok"
            ;;
        setoption*)
            rest=${line#setoption name }
            name=${rest%% value *}
            value=${rest#* value }
            if [ -n "$TT_MOCK_OPTIONS_LOG" ]; then
                printf '%s=%s\n' "$name" "$value" >>"$TT_MOCK_OPTIONS_LOG"
            fi
            case "$name" in
                MultiPV) multipv=$value ;;
            esac
            ;;
        go*)
            # A scoreless info line first: the driver must not let it blank a
            # slot that a later scored line fills.
            echo "info depth 1 currmove e2e4 currmovenumber 1"
            i=1
            while [ "$i" -le "$multipv" ]; do
                if [ "$i" -eq 1 ]; then
                    s=$score_spec
                else
                    s="cp $((25 - (i - 1) * 10))"
                fi
                echo "info depth 12 nodes 4096 time 7 nps 585142 multipv $i score $s pv $(pv_for $i)"
                i=$((i + 1))
            done
            echo "bestmove e2e4"
            ;;
        quit)
            exit 0
            ;;
    esac
done
