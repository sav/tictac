// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Command-line application: argument parsing and run-loop interface.

#pragma once

namespace tictac {

struct RunOptions;

// The command-line application: owns argument parsing and the run loop.
class App {
public:
    App(int argc, char **argv) : argc_(argc), argv_(argv) {}
    int run() const;

private:
    int const argc_;
    char **const argv_;
};

} // namespace tictac
