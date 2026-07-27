// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// The run: reads the databases and drives the games through the pipelines.

#include "runtime.hpp"

#include "game.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <print>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace tictac {

namespace {

namespace detail {

// Opening the output truncates it, and that happens before run() reads any
// input, so an --output that names an --file would destroy the database before
// it is parsed. Refuse instead.
void rejectOutputOverInput(RunOptions const &opts) {
    if (opts.noOutput || opts.output == "-") return;
    std::error_code ec;
    for (auto const &file : opts.files) {
        if (file == "-") continue;
        // equivalent() fails when the output does not exist yet, which is the
        // common case and is not a collision.
        if (std::filesystem::equivalent(file, opts.output, ec))
            throw std::runtime_error("refusing to write output over input file: " + file);
    }
}

} // namespace detail

} // namespace

Runtime::Runtime(RunOptions opts) : opts_(std::move(opts)) {
    detail::rejectOutputOverInput(opts_);
    if (!opts_.noOutput) {
        out_ = opts_.output == "-" ? std::make_shared<Writer>() : std::make_shared<Writer>(opts_.output);
        // So a plugin that opens the --output path writes to the same stream the
        // runtime emits games on, instead of a second one truncating it.
        if (opts_.output != "-") writers_.adopt(opts_.output, out_);
    }
    pipelines_.push_back(std::make_unique<Pipeline>(opts_, out_, writers_));
}

int Runtime::run() {
    for (auto &pipeline : pipelines_)
        if (!pipeline->init()) return 1;

    std::size_t index = 0;
    bool aborted = false;
    for (auto const &file : opts_.files) {
        if (aborted) break;
        std::ifstream in(file);
        if (!in) {
            std::println(stderr, "error: cannot open file: {}", file);
            return 1;
        }
        // Games stream through the pipeline one at a time: the parser hands over
        // each game as it completes, so a database never sits in memory whole.
        parseGames(in, [&](std::shared_ptr<Game> const &game) {
            ++index;
            if (pipelines_.front()->process(game, index)) aborted = true;
            return !aborted;
        });
    }
    for (auto &pipeline : pipelines_) pipeline->finish();
    for (auto &pipeline : pipelines_) pipeline->closeEngines();
    return 0;
}

} // namespace tictac
