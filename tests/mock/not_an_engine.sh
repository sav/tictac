#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# A binary that is not a UCI engine: it never answers "uci", it just exits. The
# driver's handshake must fail cleanly (and release the pipes and child) rather
# than block waiting for a "uciok" that will never arrive.

echo "this is not a chess engine"
exit 0
