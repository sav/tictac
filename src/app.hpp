#pragma once

#include <optional>

#include "runtime.hpp"

namespace tictac {

// The command-line application: owns argument parsing and the run loop.
class App {
public:
    App(int argc, char **argv) : argc_(argc), argv_(argv) {}
    int run() const;

private:
    // Fills opts from the command line. Returns an exit code when the program
    // should stop early (--help, --version, or a parse error), else nullopt.
    std::optional<int> parseArgs(RunOptions &opts) const;

    const int argc_;
    char **const argv_;
};

} // namespace tictac
