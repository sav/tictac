// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Run configuration: plugin spec parsing.

#include "options.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace tictac {

PluginSpec parsePluginSpec(std::string const &spec) {
    PluginSpec plugin;
    std::istringstream iss(spec);
    for (std::string tok; iss >> tok;) {
        if (plugin.path.empty()) {
            plugin.path = tok;
            continue;
        }
        // Whitespace tokenization means a value cannot contain a space. A bare
        // token becomes "true", and a repeated key is kept, not overwritten.
        auto eq = tok.find('=');
        if (eq == std::string::npos) plugin.args.emplace_back(tok, "true");
        else plugin.args.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
    }
    if (plugin.path.empty()) throw std::runtime_error("empty plugin spec");
    return plugin;
}

} // namespace tictac
