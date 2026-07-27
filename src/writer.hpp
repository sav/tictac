// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Output sinks: the default game stream and plugin-opened files.

#pragma once

#include "game.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <print>
#include <stdexcept>
#include <string>

namespace tictac {

// An output sink: a PGN/CSV/text file or the program's default output (stdout).
class Writer {
public:
    Writer() = default;

    explicit Writer(std::string const &path, bool append = false) {
        file_.open(path, append ? std::ios::app : std::ios::trunc);
        if (!file_) throw std::runtime_error("cannot open file: " + path);
        os_ = &file_;
    }

    Writer(Writer const &) = delete;
    Writer &operator=(Writer const &) = delete;
    // Not movable: os_ points at this object's own file_ member.
    Writer(Writer &&) = delete;
    Writer &operator=(Writer &&) = delete;

    void write(std::string const &text) {
        std::scoped_lock const lock(mu_);
        std::print(*os_, "{}", text);
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

    void writeGame(std::shared_ptr<Game> const &game) {
        // Serialized outside the lock: the game belongs to the calling worker,
        // and holding the sink while formatting would stall every other one.
        std::string const text = game->pgn();
        std::scoped_lock const lock(mu_);
        std::print(*os_, "{}\n", text);
        os_->flush();
        if (!*os_) throw std::runtime_error("writer: write failed");
    }

private:
    // One record per lock, so records from parallel workers never interleave.
    std::mutex mu_;
    std::ofstream file_;            // closed (and flushed) on destruction
    std::ostream *os_ = &std::cout; // borrows file_ when open, else std::cout
};

} // namespace tictac
