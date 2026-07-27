// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Writer registry: one output sink per path, shared across the whole run.

#include "writer.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace tictac {

namespace {

namespace detail {

// weakly_canonical resolves the part of the path that exists and normalizes the
// rest, so it also keys a file ctx.open is about to create -- which is the
// common case. It runs on an absolute path because libstdc++ hands a relative
// one straight back when none of its tail exists, which would key the same file
// differently before and after the first open created it. Best effort: a hard
// link, or a symlink made mid-run, still aliases two keys onto one file.
std::string canonicalKey(std::string const &path) {
    std::error_code ec;
    std::filesystem::path const full = std::filesystem::absolute(path, ec);
    if (ec) return std::filesystem::path(path).lexically_normal().string();
    std::filesystem::path const resolved = std::filesystem::weakly_canonical(full, ec);
    if (ec) return full.lexically_normal().string();
    return resolved.string();
}

} // namespace detail

} // namespace

std::shared_ptr<Writer> WriterRegistry::open(std::string const &path, bool append) {
    std::string key = detail::canonicalKey(path);
    std::scoped_lock const lock(mu_);
    auto const it = writers_.find(key);
    if (it != writers_.end()) {
        // The first open already decided whether the file was truncated, so a
        // second one in the other mode cannot be honoured either way: whichever
        // plugin guessed wrong would silently lose what it wrote.
        if (it->second.append != append)
            throw std::runtime_error(
                "open: '" + path + "' is already open with mode '" + (it->second.append ? "a" : "w") + "'"
            );
        return it->second.writer;
    }
    auto writer = std::make_shared<Writer>(path, append);
    writers_.emplace(std::move(key), Entry{writer, append});
    return writer;
}

void WriterRegistry::adopt(std::string const &path, std::shared_ptr<Writer> writer) {
    std::string key = detail::canonicalKey(path);
    std::scoped_lock const lock(mu_);
    writers_.insert_or_assign(std::move(key), Entry{std::move(writer), false});
}

} // namespace tictac
