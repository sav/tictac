#include "app.hpp"

#include <cstdio>
#include <print>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

namespace tictac {

std::optional<int> App::parseArgs(RunOptions &opts) const {
    CLI::App app{"tictac - stream a PGN database through a pipeline of Lua plugins"};
    app.set_version_flag("--version", "tictac " TICTAC_VERSION);

    std::vector<std::string> files;
    app.add_option("-f,--file", files, "Input PGN database (repeatable; concatenated)")
        ->required()
        ->check(CLI::ExistingFile);

    std::vector<std::string> plugin_specs;
    app.add_option("-p,--plugin", plugin_specs, "Plugin spec: \"file.lua key=value ...\" (repeatable)");

    std::string output = "-";
    app.add_option("-o,--output", output, "Where surviving games are written (default: stdout)")
        ->check([](const std::string &s) { return s.empty() ? std::string("must not be empty") : std::string(); });

    bool no_output = false;
    app.add_flag("--no-output", no_output, "Discard the default game stream");

    std::string on_error = "abort";
    app.add_option("--on-error", on_error, "On plugin error: abort | drop | pass")
        ->check(CLI::IsMember({"abort", "drop", "pass"}));

    int jobs = 1;
    app.add_option("-j,--jobs", jobs, "Reserved: parallel game workers");

    try {
        app.parse(argc_, argv_);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    opts.files = files;
    opts.output = output;
    opts.noOutput = no_output;
    opts.jobs = jobs;
    if (on_error == "pass") opts.onError = OnError::Pass;
    else if (on_error == "drop") opts.onError = OnError::Drop;
    else opts.onError = OnError::Abort;

    for (const auto &spec : plugin_specs) {
        opts.plugins.push_back(parsePluginSpec(spec));
    }

    return std::nullopt;
}

int App::run() const {
    try {
        RunOptions opts;
        if (auto code = parseArgs(opts)) return *code;
        Runtime runtime(std::move(opts));
        return runtime.run();
    } catch (const std::exception &e) {
        std::println(stderr, "error: {}", e.what());
        return 1;
    }
}

} // namespace tictac
