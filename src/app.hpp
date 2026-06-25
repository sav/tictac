#pragma once

#include <optional>

#include "runtime.hpp"

namespace tictac {

// The command-line application: owns argument parsing and the run loop.
class App {
public:
    App(int argc, char **argv);
    int run();

private:
    // Fills opts from the command line. Returns an exit code when the program
    // should stop early (--help, --version, or a parse error), else nullopt.
    std::optional<int> parseArgs(RunOptions &opts);

    int argc_;
    char **argv_;
};

} // namespace tictac
