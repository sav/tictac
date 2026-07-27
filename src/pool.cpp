// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// Worker pool: runs games through a fixed set of pipelines, one thread each.

#include "pool.hpp"

#include <exception>
#include <mutex>
#include <utility>

namespace tictac {

GamePool::GamePool(std::vector<std::unique_ptr<Pipeline>> const &pipelines)
    : capacity_(pipelines.size()) {
    workers_.reserve(pipelines.size());
    for (auto const &pipeline : pipelines)
        workers_.emplace_back([this, p = pipeline.get()](std::stop_token stop) { work(stop, *p); });
}

GamePool::~GamePool() {
    // drain() is the normal path; this covers an exception unwinding past it.
    // ~jthread would request a stop and join anyway, but draining here keeps the
    // mutex and condition variables alive while the workers still need them.
    try {
        drain();
    } catch (...) {
        // A join failure during teardown has nowhere left to report to.
    }
}

bool GamePool::submit(Job job) {
    {
        std::unique_lock lock(mu_);
        free_.wait(lock, [this] { return inflight_ < capacity_ || stopping_; });
        if (stopping_) return false;
        queue_.push_back(std::move(job));
        ++inflight_;
    }
    ready_.notify_one();
    return true;
}

bool GamePool::stopped() {
    std::scoped_lock const lock(mu_);
    return stopping_;
}

void GamePool::drain() {
    {
        std::scoped_lock const lock(mu_);
        if (closed_) return;
        closed_ = true;
    }
    ready_.notify_all();
    for (auto &worker : workers_) worker.join();
}

std::exception_ptr GamePool::error() {
    std::scoped_lock const lock(mu_);
    return error_;
}

void GamePool::work(std::stop_token const &stop, Pipeline &pipeline) {
    // Without this a stop request cannot wake a worker parked on ready_, and
    // ~jthread would join a thread that never gets to check the token.
    std::stop_callback const wake(stop, [this] { ready_.notify_all(); });

    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mu_);
            ready_.wait(lock, [&] { return !queue_.empty() || closed_ || stop.stop_requested(); });
            if (queue_.empty()) return; // closed or cancelled, and nothing left to run
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        bool finished = false;
        std::exception_ptr failed;
        try {
            finished = pipeline.process(job.game, job.index);
        } catch (...) {
            failed = std::current_exception(); // must not escape the thread
        }

        {
            std::scoped_lock const lock(mu_);
            --inflight_;
            if (finished) stopping_ = true;
            if (failed) {
                stopping_ = true;
                if (!error_) error_ = failed; // the first abort is the one reported
                // An abort ends the run as soon as it can: games already running
                // finish, but ones that never started are dropped.
                inflight_ -= queue_.size();
                queue_.clear();
            }
        }
        free_.notify_all();
    }
}

} // namespace tictac
