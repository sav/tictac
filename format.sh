#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
#
# Format all tracked C++ source files with clang-format.

git ls-files -- '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx' | xargs clang-format -i
