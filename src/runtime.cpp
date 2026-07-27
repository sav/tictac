// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// The run: reads the databases and drives the games through the pipelines.

#include "runtime.hpp"

#include "game.hpp"
#include "pool.hpp"

#include <cstdio>
#include <exception>
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
    // Built here, on one thread, before any worker exists: loading the plugins
    // also warms sol2's lazily-initialized usertype tables, and doing that from
    // several threads at once would race.
    pipelines_.reserve(opts_.jobs);
    for (std::size_t i = 0; i < opts_.jobs; ++i)
        pipelines_.push_back(std::make_unique<Pipeline>(opts_, out_, writers_, i + 1, opts_.jobs));
}

int Runtime::run() {
    // init() runs on this thread, before any worker exists: it serializes the
    // engine forks a plugin may do there and keeps error reporting in order.
    for (auto &pipeline : pipelines_)
        if (!pipeline->init()) return 1;

    // A local, so every exit path drains and joins the workers.
    GamePool pool(pipelines_);
    std::size_t index = 0;
    for (auto const &file : opts_.files) {
        if (pool.stopped()) break;
        std::ifstream in(file);
        if (!in) {
            std::println(stderr, "error: cannot open file: {}", file);
            return 1;
        }
        // Games stream through the pipelines one at a time: the parser hands
        // over each game as it completes, so a database never sits in memory
        // whole. submit() blocks while every worker is busy, which is what
        // bounds that to the number of pipelines.
        parseGames(in, [&](std::shared_ptr<Game> const &game) {
            ++index;
            return pool.submit({game, index});
        });
    }
    pool.drain();
    // Rethrown before finish(): under --on-error abort the exception used to
    // unwind straight out of run(), so the finish hooks never ran.
    if (std::exception_ptr const err = pool.error()) std::rethrow_exception(err);

    for (auto &pipeline : pipelines_) pipeline->finish();
    for (auto &pipeline : pipelines_) pipeline->closeEngines();
    return 0;
}

} // namespace tictac
