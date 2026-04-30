#include "engine/uci_engine.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace tictac {

namespace {

constexpr const char* kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

void ignore_sigpipe_once() {
    static bool done = false;
    if (done) return;
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGPIPE, &sa, nullptr);
    done = true;
}

void parse_info_line(const std::string& line, std::map<unsigned, PvLine>& latest) {
    std::istringstream iss(line);
    std::string tok;
    iss >> tok; // "info"
    PvLine pv;
    unsigned multipv = 1;
    bool have_data = false;
    while (iss >> tok) {
        if (tok == "depth") {
            iss >> pv.depth; have_data = true;
        } else if (tok == "seldepth") {
            iss >> pv.seldepth; have_data = true;
        } else if (tok == "multipv") {
            iss >> multipv;
        } else if (tok == "nodes") {
            iss >> pv.nodes; have_data = true;
        } else if (tok == "score") {
            std::string kind;
            iss >> kind;
            if (kind == "cp") {
                iss >> pv.score_cp;
                pv.is_mate = false;
            } else if (kind == "mate") {
                iss >> pv.mate_in;
                pv.is_mate = true;
            }
            have_data = true;
        } else if (tok == "pv") {
            std::string m;
            while (iss >> m) pv.pv.push_back(m);
            have_data = true;
            break;
        }
        // Unknown tokens fall through; their value (if any) becomes the next "tok"
        // and is ignored too. Adequate for stockfish's typical layout.
    }
    if (have_data) latest[multipv] = std::move(pv);
}

} // namespace

UciEngine::~UciEngine() { stop(); }

bool UciEngine::is_running() const { return pid_ > 0; }

bool UciEngine::start(const std::filesystem::path& path) {
    if (is_running()) return true;
    ignore_sigpipe_once();

    int in_pipe[2]  = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (::pipe(in_pipe) < 0) return false;
    if (::pipe(out_pipe) < 0) {
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        return false;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in_pipe[0]);  ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        // child: stdin <- in_pipe read end, stdout -> out_pipe write end
        ::dup2(in_pipe[0],  STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        int dn = ::open("/dev/null", O_WRONLY);
        if (dn >= 0) { ::dup2(dn, STDERR_FILENO); ::close(dn); }
        ::close(in_pipe[0]);  ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::execlp(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    // parent
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    stdin_fd_    = in_pipe[1];
    stdout_file_ = ::fdopen(out_pipe[0], "r");
    if (!stdout_file_) {
        ::close(out_pipe[0]);
        ::close(stdin_fd_); stdin_fd_ = -1;
        ::kill(pid, SIGTERM);
        ::waitpid(pid, nullptr, 0);
        return false;
    }
    pid_ = pid;

    // UCI handshake
    try {
        send_line("uci");
        while (true) {
            auto line = read_line();
            if (!line) { stop(); return false; }
            if (line->rfind("id name ", 0) == 0) {
                name_ = line->substr(8);
            }
            if (*line == "uciok") break;
        }
        send_line("isready");
        wait_for_token("readyok");
    } catch (...) {
        stop();
        return false;
    }
    return true;
}

void UciEngine::stop() {
    if (pid_ <= 0) {
        if (stdout_file_) { std::fclose(stdout_file_); stdout_file_ = nullptr; }
        if (stdin_fd_ >= 0) { ::close(stdin_fd_); stdin_fd_ = -1; }
        return;
    }
    if (stdin_fd_ >= 0) {
        const char* q = "quit\n";
        ::write(stdin_fd_, q, 5); // best-effort
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_file_) {
        std::fclose(stdout_file_);
        stdout_file_ = nullptr;
    }
    int status = 0;
    if (::waitpid(pid_, &status, WNOHANG) == 0) {
        ::kill(pid_, SIGTERM);
        ::waitpid(pid_, &status, 0);
    }
    pid_ = -1;
}

void UciEngine::send_line(std::string_view line) {
    if (stdin_fd_ < 0) throw std::runtime_error("UCI engine not running");
    std::string buf{line};
    buf.push_back('\n');
    const char* p = buf.data();
    std::size_t left = buf.size();
    while (left > 0) {
        ssize_t n = ::write(stdin_fd_, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("UCI engine write failed");
        }
        p    += n;
        left -= static_cast<std::size_t>(n);
    }
}

std::optional<std::string> UciEngine::read_line() {
    if (!stdout_file_) return std::nullopt;
    char* buf = nullptr;
    std::size_t cap = 0;
    ssize_t n = ::getline(&buf, &cap, stdout_file_);
    if (n <= 0) {
        std::free(buf);
        return std::nullopt;
    }
    std::string s(buf, static_cast<std::size_t>(n));
    std::free(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

void UciEngine::wait_for_token(std::string_view token) {
    while (true) {
        auto line = read_line();
        if (!line) throw std::runtime_error("UCI engine closed unexpectedly");
        if (*line == token) return;
        if (line->rfind(std::string(token) + " ", 0) == 0) return;
    }
}

void UciEngine::set_position(std::string_view fen) {
    if (fen.empty() || fen == kStartFen) {
        send_line("position startpos");
    } else {
        std::string cmd = "position fen ";
        cmd.append(fen);
        send_line(cmd);
    }
}

void UciEngine::set_position(std::string_view fen,
                             const std::vector<std::string>& moves) {
    std::string cmd;
    if (fen.empty() || fen == kStartFen) {
        cmd = "position startpos";
    } else {
        cmd = "position fen ";
        cmd.append(fen);
    }
    if (!moves.empty()) {
        cmd += " moves";
        for (const auto& m : moves) {
            cmd += ' ';
            cmd += m;
        }
    }
    send_line(cmd);
}

void UciEngine::set_option(std::string_view name, std::string_view value) {
    std::string cmd = "setoption name ";
    cmd.append(name);
    cmd.append(" value ");
    cmd.append(value);
    send_line(cmd);
}

AnalysisResult UciEngine::analyze(std::chrono::milliseconds time_limit,
                                  unsigned depth, unsigned multi_pv) {
    if (multi_pv == 0) multi_pv = 1;
    if (multi_pv != current_multipv_) {
        set_option("MultiPV", std::to_string(multi_pv));
        current_multipv_ = multi_pv;
    }

    send_line("isready");
    wait_for_token("readyok");

    std::string go = "go";
    if (depth > 0)               { go += " depth ";    go += std::to_string(depth); }
    if (time_limit.count() > 0)  { go += " movetime "; go += std::to_string(time_limit.count()); }
    if (depth == 0 && time_limit.count() == 0) {
        // Safe default — depth 10 finishes within ~tens of ms on modern hardware.
        go += " depth 10";
    }
    send_line(go);

    std::map<unsigned, PvLine> latest;
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        auto line = read_line();
        if (!line) throw std::runtime_error("UCI engine closed during analyze");
        if (line->rfind("info ", 0) == 0) {
            parse_info_line(*line, latest);
        } else if (line->rfind("bestmove", 0) == 0) {
            break;
        }
        // Ignore everything else (e.g. banner / "info string ..." debug output).
    }
    auto t1 = std::chrono::steady_clock::now();

    AnalysisResult r;
    r.lines.reserve(latest.size());
    for (auto& kv : latest) {
        r.total_nodes += kv.second.nodes;
        r.lines.push_back(std::move(kv.second));
    }
    r.time = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
    return r;
}

} // namespace tictac
