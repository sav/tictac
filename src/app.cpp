// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Command-line application: CLI11 argument parsing and run loop.

#include "app.hpp"
#include "options.hpp"
#include "runtime.hpp"

#include <cstdio>
#include <optional>
#include <print>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

namespace tictac {

namespace {

// Fills opts from the command line. Returns an exit code when the program
// should stop early (--help, --version, or a parse error), else nullopt.
[[nodiscard]] std::optional<int> parseArgs(int argc, char **argv, RunOptions &opts) {
    CLI::App app{"tictac - stream a PGN database through a pipeline of Lua plugins"};
    app.set_version_flag("--version", "tictac " TICTAC_VERSION);

    std::vector<std::string> files;
    app.add_option("-f,--file", files, "Input PGN database (repeatable; concatenated)")
        ->required()
        ->check(CLI::ExistingFile);

    std::vector<std::string> plugin_specs;
    app.add_option("-p,--plugin", plugin_specs, "Plugin spec: \"file.lua key=value ...\" (repeatable)")
        ->required();

    std::string output = "-";
    app.add_option("-o,--output", output, "Where surviving games are written (default: stdout)")
        ->check([](std::string const &s) {
            return s.empty() ? std::string("must not be empty") : std::string();
        });

    bool no_output = false;
    app.add_flag("--no-output", no_output, "Discard the default game stream");

    std::string on_error = "abort";
    app.add_option("--on-error", on_error, "On plugin error: abort | drop | pass")
        ->check(CLI::IsMember({"abort", "drop", "pass"}));

    try {
        app.parse(argc, argv);
    } catch (CLI::ParseError const &e) {
        return app.exit(e);
    }

    opts.files = files;
    opts.output = output;
    opts.noOutput = no_output;
    if (on_error == "pass") opts.onError = OnError::Pass;
    else if (on_error == "drop") opts.onError = OnError::Drop;
    else opts.onError = OnError::Abort;

    for (auto const &spec : plugin_specs) opts.plugins.push_back(parsePluginSpec(spec));
    return std::nullopt;
}

} // namespace

int App::run() const {
    try {
        RunOptions opts;
        if (auto code = parseArgs(argc_, argv_, opts)) return *code;
        Runtime runtime(std::move(opts));
        return runtime.run();
    } catch (std::exception const &e) {
        std::println(stderr, "error: {}", e.what());
        return 1;
    }
}

} // namespace tictac
