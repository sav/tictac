// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
//
// UCI chess engine driver: subprocess management and UCI protocol I/O.

#include "engine.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tictac {

Engine::Engine(const std::string &path, const std::map<std::string, std::string> &options) {
    int in_pipe[2];  // parent -> child stdin
    int out_pipe[2]; // child stdout -> parent
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        throw std::runtime_error("engine: failed to create pipes");
    }

    pid_ = fork();
    if (pid_ < 0) {
        throw std::runtime_error("engine: fork failed");
    }

    if (pid_ == 0) {
        // Child: wire pipes to stdin/stdout and exec the engine.
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execlp(path.c_str(), path.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    to_engine_ = in_pipe[1];
    from_engine_ = out_pipe[0];

    send("uci");
    waitFor("uciok");
    for (const auto &[name, value] : options) {
        setOption(name, value);
    }
    send("isready");
    waitFor("readyok");
}

Engine::~Engine() {
    if (pid_ > 0) {
        send("quit");
        close(to_engine_);
        close(from_engine_);
        int status = 0;
        if (waitpid(pid_, &status, WNOHANG) == 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, &status, 0);
        }
    }
}

void Engine::send(const std::string &line) {
    std::string data = line + "\n";
    ssize_t off = 0;
    while (off < static_cast<ssize_t>(data.size())) {
        ssize_t n = write(to_engine_, data.data() + off, data.size() - off);
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
        ssize_t n = read(from_engine_, buf.data(), buf.size());
        if (n <= 0) throw std::runtime_error("engine: process closed unexpectedly");
        read_buffer_.append(buf.data(), static_cast<std::size_t>(n));
    }
}

void Engine::waitFor(const std::string &token) {
    for (;;) {
        if (readLine() == token) return;
    }
}

void Engine::setOption(const std::string &name, const std::string &value) {
    send("setoption name " + name + " value " + value);
}

Analysis Engine::analyse(const std::string &fen, const AnalysisLimits &limits) {
    if (!limits.depth && !limits.movetime && !limits.nodes) {
        throw std::runtime_error("engine:analyse requires one of depth, movetime or nodes");
    }

    setOption("MultiPV", std::to_string(limits.multipv));
    send("isready");
    waitFor("readyok");
    send("position fen " + fen);

    std::ostringstream go;
    go << "go";
    if (limits.depth) go << " depth " << *limits.depth;
    if (limits.movetime) go << " movetime " << *limits.movetime;
    if (limits.nodes) go << " nodes " << *limits.nodes;
    send(go.str());

    Analysis result;
    std::vector<AnalysisLine> lines(static_cast<std::size_t>(std::max(1, limits.multipv)));
    bool seen[64] = {false};

    for (;;) {
        std::string line = readLine();
        std::istringstream iss(line);
        std::string tok;
        iss >> tok;

        if (tok == "bestmove") {
            iss >> result.bestmove;
            break;
        }
        if (tok != "info") continue;

        AnalysisLine cur;
        int multipv = 1;
        bool has_score = false;
        while (iss >> tok) {
            if (tok == "depth") {
                iss >> result.depth;
            } else if (tok == "nodes") {
                iss >> result.nodes;
            } else if (tok == "time") {
                iss >> result.time;
            } else if (tok == "nps") {
                iss >> result.nps;
            } else if (tok == "multipv") {
                iss >> multipv;
            } else if (tok == "score") {
                std::string kind;
                iss >> kind;
                if (kind == "cp") {
                    int cp = 0;
                    iss >> cp;
                    cur.score = static_cast<double>(cp);
                    has_score = true;
                } else if (kind == "mate") {
                    int m = 0;
                    iss >> m;
                    cur.mate = m;
                    has_score = true;
                }
            } else if (tok == "pv") {
                std::string mv;
                while (iss >> mv) cur.pv.push_back(mv);
            }
        }

        if (has_score && multipv >= 1 && multipv <= static_cast<int>(lines.size())) {
            lines[static_cast<std::size_t>(multipv - 1)] = cur;
            if (multipv < 64) seen[multipv] = true;
        }
    }

    result.score = lines[0].score;
    result.mate = lines[0].mate;
    result.pv = lines[0].pv;
    if (limits.multipv > 1) {
        for (int i = 1; i <= limits.multipv && i < 64; ++i) {
            if (seen[i]) result.lines.push_back(lines[static_cast<std::size_t>(i - 1)]);
        }
    }
    return result;
}

} // namespace tictac
