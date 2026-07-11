// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Program entry point: construct the App and run it.

#include "app.hpp"

int main(int argc, char *argv[]) {
    tictac::App app(argc, argv);
    return app.run();
}
