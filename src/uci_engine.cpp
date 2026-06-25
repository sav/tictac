#include "uci_engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <reproc++/reproc.hpp>

namespace uci {

namespace {

std::string limitString(const Limits& limits) {
    if (limits.depth) return "depth " + std::to_string(*limits.depth);
    if (limits.movetime_ms) return "movetime " + std::to_string(*limits.movetime_ms);
    if (limits.nodes) return "nodes " + std::to_string(*limits.nodes);
    return "depth 12";
}

}  // namespace

struct UciEngine::Impl {
    struct Request {
        bool newgame = false;
        std::string fen;
        std::promise<Analysis> promise;
    };

    reproc::process proc;
    std::string limit;
    std::string rbuf;

    std::deque<Request> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;
    std::thread worker;

    void send(const std::string& cmd) {
        std::string line = cmd;
        line.push_back('\n');
        const auto* p = reinterpret_cast<const uint8_t*>(line.data());
        size_t left = line.size();
        while (left > 0) {
            auto [n, ec] = proc.write(p, left);
            if (ec) throw std::system_error(ec, "uci: write failed");
            p += n;
            left -= n;
        }
    }

    std::optional<std::string> readLine() {
        for (;;) {
            if (auto nl = rbuf.find('\n'); nl != std::string::npos) {
                std::string line = rbuf.substr(0, nl);
                rbuf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return line;
            }
            uint8_t tmp[4096];
            auto [n, ec] = proc.read(reproc::stream::out, tmp, sizeof(tmp));
            if (n > 0) rbuf.append(reinterpret_cast<char*>(tmp), n);
            if (ec) {
                if (!rbuf.empty()) {
                    std::string line = rbuf;
                    rbuf.clear();
                    return line;
                }
                return std::nullopt;
            }
        }
    }

    void waitFor(std::string_view token) {
        while (auto line = readLine()) {
            if (line->rfind(token, 0) == 0) return;
        }
        throw std::runtime_error("uci: engine closed before '" + std::string(token) + "'");
    }

    static void parseInfo(const std::string& line, std::map<int, PvLine>& lines) {
        std::istringstream is(line);
        std::string tok;
        is >> tok;  // "info"
        PvLine pv;
        bool has_payload = false;
        while (is >> tok) {
            if (tok == "string") return;
            if (tok == "depth") {
                is >> pv.depth;
            } else if (tok == "multipv") {
                is >> pv.multipv;
            } else if (tok == "score") {
                std::string kind;
                int value = 0;
                is >> kind >> value;
                if (kind == "cp") pv.cp = value;
                else if (kind == "mate") pv.mate = value;
                has_payload = true;
            } else if (tok == "pv") {
                std::string rest, m;
                while (is >> m) {
                    if (!rest.empty()) rest.push_back(' ');
                    rest += m;
                }
                pv.moves = rest;
                has_payload = true;
            }
        }
        if (has_payload) lines[pv.multipv] = std::move(pv);
    }

    Analysis runAnalyze(const std::string& fen) {
        send("position fen " + fen);
        send("go " + limit);

        std::map<int, PvLine> lines;
        Analysis result;
        result.fen = fen;
        while (auto line = readLine()) {
            if (line->rfind("bestmove", 0) == 0) {
                std::istringstream is(*line);
                std::string tag;
                is >> tag >> result.bestmove;
                break;
            }
            if (line->rfind("info", 0) == 0) parseInfo(*line, lines);
        }
        for (auto& [idx, pv] : lines) result.lines.push_back(std::move(pv));
        return result;
    }

    void run() {
        for (;;) {
            Request req;
            {
                std::unique_lock lock(mtx);
                cv.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) break;
                req = std::move(queue.front());
                queue.pop_front();
            }
            try {
                if (req.newgame) {
                    send("ucinewgame");
                    send("isready");
                    waitFor("readyok");
                } else {
                    req.promise.set_value(runAnalyze(req.fen));
                }
            } catch (...) {
                if (!req.newgame) req.promise.set_exception(std::current_exception());
            }
        }
        std::lock_guard lock(mtx);
        for (auto& req : queue) {
            if (!req.newgame) {
                req.promise.set_exception(std::make_exception_ptr(
                    std::runtime_error("uci: engine shutting down")));
            }
        }
        queue.clear();
    }
};

UciEngine::UciEngine(EngineConfig config, Limits limits)
    : impl_(std::make_unique<Impl>()) {
    impl_->limit = limitString(limits);

    std::vector<std::string> argv;
    argv.push_back(config.path);
    for (auto& a : config.args) argv.push_back(a);

    reproc::options options;
    options.redirect.err.type = reproc::redirect::discard;

    if (auto ec = impl_->proc.start(argv, options)) {
        throw std::system_error(ec, "uci: failed to start engine '" + config.path + "'");
    }

    impl_->send("uci");
    impl_->waitFor("uciok");
    for (auto& [name, value] : config.options) {
        impl_->send("setoption name " + name + " value " + value);
    }
    if (config.multipv > 1) {
        impl_->send("setoption name MultiPV value " + std::to_string(config.multipv));
    }
    impl_->send("isready");
    impl_->waitFor("readyok");

    impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

UciEngine::~UciEngine() {
    if (!impl_) return;
    {
        std::lock_guard lock(impl_->mtx);
        impl_->stop = true;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();

    try {
        impl_->send("quit");
    } catch (...) {
    }
    impl_->proc.close(reproc::stream::in);
    impl_->proc.stop({{reproc::stop::wait, reproc::milliseconds(200)},
                      {reproc::stop::terminate, reproc::milliseconds(200)},
                      {reproc::stop::kill, reproc::milliseconds(200)}});
}

void UciEngine::newGame() {
    Impl::Request req;
    req.newgame = true;
    {
        std::lock_guard lock(impl_->mtx);
        impl_->queue.push_back(std::move(req));
    }
    impl_->cv.notify_all();
}

std::future<Analysis> UciEngine::analyze(std::string fen) {
    Impl::Request req;
    req.fen = std::move(fen);
    std::future<Analysis> fut = req.promise.get_future();
    {
        std::lock_guard lock(impl_->mtx);
        impl_->queue.push_back(std::move(req));
    }
    impl_->cv.notify_all();
    return fut;
}

}  // namespace uci
