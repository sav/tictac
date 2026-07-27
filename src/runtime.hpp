// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// The run: reads the databases and drives the games through the pipelines.

#pragma once

#include "options.hpp"
#include "pipeline.hpp"
#include "writer.hpp"

#include <memory>
#include <vector>

namespace tictac {

class Runtime {
public:
    explicit Runtime(RunOptions opts);
    int run();

private:
    // opts_ backs the reference every pipeline holds, and the pipelines are
    // torn down before out_ and writers_ close their files, so the declaration
    // order here is load-bearing.
    RunOptions opts_;
    std::shared_ptr<Writer> out_; // the default game stream; null under --no-output
    WriterRegistry writers_;      // ctx.open() sinks, one per path for the whole run
    std::vector<std::unique_ptr<Pipeline>> pipelines_;
};

} // namespace tictac
