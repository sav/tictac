// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Run configuration: the options a command line resolves into.

#pragma once

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
};

// Parse a "file.lua key=value key2=value2" spec into a PluginSpec.
[[nodiscard]] PluginSpec parsePluginSpec(std::string const &spec);

} // namespace tictac
