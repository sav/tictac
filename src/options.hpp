// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Run configuration: the options a command line resolves into.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tictac {

enum class OnError { Abort, Drop, Pass };

struct PluginSpec {
    std::string path;
    std::vector<std::pair<std::string, std::string>> args;
};

struct RunOptions {
    std::vector<std::string> files;
    std::vector<PluginSpec> plugins;
    std::string output = "-";
    bool noOutput = false;
    OnError onError = OnError::Abort;
    std::size_t jobs = 1; // pipelines run in parallel; always >= 1 by the time run() sees it
};

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
[[nodiscard]] PluginSpec parsePluginSpec(std::string const &spec);

} // namespace tictac
