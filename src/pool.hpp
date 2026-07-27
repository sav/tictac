// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Worker pool: runs games through a fixed set of pipelines, one thread each.

#pragma once

#include "game.hpp"
#include "runtime.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tictac {

// One game waiting for a worker.
struct Job {
    std::shared_ptr<Game> game;
    std::size_t index = 0;
};

// Runs games through a fixed set of pipelines: one worker thread per pipeline,
// bound for the pool's lifetime, so no pipeline is ever touched by two threads.
//
// The producer -- the thread reading the PGN -- calls submit(). It blocks while
// every worker is busy, so queued plus running never exceeds the pipeline count.
// With one pipeline that reduces to sequential order: submit() for game k+1
// cannot return until game k has finished, so a stop from k is always seen
// before k+1 is ever dispatched.
class GamePool {
public:
    // `pipelines` must outlive the pool and must not be empty.
    explicit GamePool(std::vector<std::unique_ptr<Pipeline>> const &pipelines);
    ~GamePool();

    GamePool(GamePool const &) = delete;
    GamePool &operator=(GamePool const &) = delete;

    // Hand a game to the next free worker, blocking while all of them are busy.
    // False once a plugin has asked to stop, meaning: read no further.
    [[nodiscard]] bool submit(Job job);

    [[nodiscard]] bool stopped();

    // Let the in-flight games finish, then join every worker. Idempotent.
    void drain();

    // The first exception a worker caught, or null. Valid once drained.
    [[nodiscard]] std::exception_ptr error();

private:
    void work(std::stop_token const &stop, Pipeline &pipeline);

    std::mutex mu_;
    std::condition_variable free_;  // a worker went idle
    std::condition_variable ready_; // a job arrived, or the pool closed
    std::deque<Job> queue_;
    std::size_t inflight_ = 0; // queued plus running; never exceeds capacity_
    std::size_t const capacity_;
    bool closed_ = false;   // no further submissions will arrive
    bool stopping_ = false; // a plugin asked to stop, or one aborted
    std::exception_ptr error_;
    std::vector<std::jthread> workers_; // declared last, so joined first
};

} // namespace tictac
