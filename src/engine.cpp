// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// UCI chess engine driver: subprocess management and UCI protocol I/O.

#include "engine.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tictac {

namespace {

constexpr int kMaxLines = 64; // cap on UCI multipv lines we track

// Writing to an engine that already exited must fail with EPIPE rather than
// raise SIGPIPE, whose default action would kill the whole process.
void ignoreSigPipeOnce() {
    [[maybe_unused]] static bool const done = [] {
        ::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
}

} // namespace

Engine::Engine(std::string const &path, std::unordered_map<std::string, std::string> const &options) {
    ignoreSigPipeOnce();
    std::array<int, 2> in_pipe{};  // parent -> child stdin
    std::array<int, 2> out_pipe{}; // child stdout -> parent
    // O_CLOEXEC so a later engine's child does not inherit these fds; otherwise it
    // would hold a duplicate of this engine's stdin write end, and closing
    // to_engine_ in shutdown() would no longer give this child its EOF. The
    // child's own stdin/stdout survive exec because dup2 clears close-on-exec.
    if (::pipe2(in_pipe.data(), O_CLOEXEC) != 0) throw std::runtime_error("engine: failed to create pipes");
    if (::pipe2(out_pipe.data(), O_CLOEXEC) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        throw std::runtime_error("engine: failed to create pipes");
    }
    pid_ = ::fork();
    if (pid_ < 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        throw std::runtime_error("engine: fork failed");
    }
    if (pid_ == 0) {
        // Child: wire pipes to stdin/stdout and exec the engine.
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::execlp(path.c_str(), path.c_str(), static_cast<char *>(nullptr));
        // Only reached when exec fails; report why before the parent sees EOF.
        // No throw: unwinding would run the parent's handlers, destructors and
        // atexit hooks in the child too, so print and _exit instead.
        std::fprintf(stderr, "engine: exec %s: %s\n", path.c_str(), std::strerror(errno));
        ::_exit(127);
    }
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    to_engine_ = in_pipe[1];
    from_engine_ = out_pipe[0];
    // The destructor does not run for a constructor that throws, so the
    // handshake has to release the pipes and reap the child itself.
    try {
        handshake(options);
    } catch (...) {
        shutdown();
        throw;
    }
}

Engine::~Engine() { shutdown(); }

void Engine::setOption(std::string const &name, std::string const &value) {
    send("setoption name " + name + " value " + value);
}

namespace {

// One parsed "info" line. `slot` is the 1-based multipv index it describes, and
// `scored` records whether it carried a score at all -- depth/seldepth/nodes/
// time/nps are engine-wide rather than per-line, so they land in `overall`.
struct InfoLine {
    AnalysisLine line;
    int slot = 1;
    bool scored = false;
};

namespace detail {

std::string goCommand(AnalysisLimits const &limits) {
    std::string go = "go";
    if (limits.depth) go += std::format(" depth {}", *limits.depth);
    if (limits.movetime) go += std::format(" movetime {}", *limits.movetime);
    if (limits.nodes) go += std::format(" nodes {}", *limits.nodes);
    return go;
}

InfoLine parseInfo(std::istringstream &iss, Analysis &overall) {
    InfoLine info;
    std::string tok;
    while (iss >> tok) {
        if (tok == "depth") {
            iss >> overall.depth;
        } else if (tok == "seldepth") {
            iss >> overall.seldepth;
        } else if (tok == "nodes") {
            iss >> overall.nodes;
        } else if (tok == "time") {
            iss >> overall.time;
        } else if (tok == "nps") {
            iss >> overall.nps;
        } else if (tok == "multipv") {
            iss >> info.slot;
        } else if (tok == "score") {
            std::string kind;
            int val = 0;
            iss >> kind >> val;
            if (kind == "cp") info.line.score = static_cast<double>(val);
            else if (kind == "mate") info.line.mate = val;
            info.scored = kind == "cp" || kind == "mate";
        } else if (tok == "pv") {
            std::string mv;
            while (iss >> mv) info.line.pv.push_back(mv);
        }
    }
    return info;
}

void collect(Analysis &result, std::vector<AnalysisLine> const &lines, int multipv) {
    result.score = lines[0].score;
    result.mate = lines[0].mate;
    result.pv = lines[0].pv;
    if (multipv <= 1) return;
    for (auto const &line : lines) {
        if (line.score || line.mate) result.lines.push_back(line);
    }
}

} // namespace detail

} // namespace

Analysis Engine::analyse(std::string const &fen, AnalysisLimits const &limits) {
    if (!limits.depth && !limits.movetime && !limits.nodes)
        throw std::runtime_error("engine: analyse requires one of depth, movetime or nodes");
    if (limits.multipv < 1 || limits.multipv > kMaxLines)
        throw std::runtime_error(std::format("engine: multipv must be between 1 and {}", kMaxLines));
    setOption("MultiPV", std::to_string(limits.multipv));
    syncReady();
    send("position fen " + fen);
    send(detail::goCommand(limits));

    Analysis result;
    // Indexed by the UCI multipv number minus one; a slot with neither score
    // nor mate is one the engine never reported.
    std::vector<AnalysisLine> lines(static_cast<std::size_t>(limits.multipv));
    for (;;) {
        std::istringstream iss(readLine());
        std::string tok;
        iss >> tok;
        if (tok == "bestmove") {
            iss >> result.bestmove;
            break;
        }
        if (tok != "info") continue;
        InfoLine info = detail::parseInfo(iss, result);
        // Scoreless info lines (currmove/hashfull) carry no pv and would blank a good slot.
        if (info.scored && info.slot >= 1 && info.slot <= static_cast<int>(lines.size()))
            lines[static_cast<std::size_t>(info.slot - 1)] = std::move(info.line);
    }
    detail::collect(result, lines, limits.multipv);
    return result;
}

void Engine::send(std::string const &line) {
    std::string data = line + "\n";
    ssize_t off = 0;
    while (off < static_cast<ssize_t>(data.size())) {
        ssize_t n =
            ::write(to_engine_, data.data() + off, data.size() - static_cast<std::size_t>(off));
        if (n <= 0) throw std::runtime_error("engine: write failed");
        off += n;
    }
}

std::string Engine::readLine() {
    for (;;) {
        auto nl = read_buffer_.find('\n');
        if (nl != std::string::npos) {
            std::string line = read_buffer_.substr(0, nl);
            read_buffer_.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        std::array<char, 4096> buf;
        ssize_t n = ::read(from_engine_, buf.data(), buf.size());
        if (n <= 0) throw std::runtime_error("engine: process closed unexpectedly");
        read_buffer_.append(buf.data(), static_cast<std::size_t>(n));
    }
}

void Engine::waitFor(std::string const &token) {
    for (;;)
        if (readLine() == token) return;
}

void Engine::syncReady() {
    send("isready");
    waitFor("readyok");
}

void Engine::handshake(std::unordered_map<std::string, std::string> const &options) {
    send("uci");
    waitFor("uciok");
    for (auto const &[name, value] : options) setOption(name, value);
    syncReady();
}

void Engine::shutdown() noexcept {
    if (pid_ <= 0) return;
    if (to_engine_ >= 0) {
        // Best effort: a dead engine makes this fail, which is fine here.
        constexpr std::string_view quit = "quit\n";
        [[maybe_unused]] ssize_t const n = ::write(to_engine_, quit.data(), quit.size());
        ::close(to_engine_);
        to_engine_ = -1;
    }
    if (from_engine_ >= 0) {
        ::close(from_engine_);
        from_engine_ = -1;
    }
    int status = 0;
    if (::waitpid(pid_, &status, WNOHANG) == 0) {
        ::kill(pid_, SIGTERM);
        ::waitpid(pid_, &status, 0);
    }
    pid_ = -1;
}

} // namespace tictac
