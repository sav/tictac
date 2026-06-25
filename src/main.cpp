#include <cstddef>
#include <deque>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <CLI/CLI.hpp>
#include <chess.hpp>

#include "uci_engine.hpp"

class GameVisitor : public chess::pgn::Visitor {
public:
    explicit GameVisitor(uci::UciEngine* engine) : engine_(engine) {}

    void startPgn() override {
        board_.setFen(chess::constants::STARTPOS);
        headers_.clear();
        pending_.clear();
        ply_ = 0;
        if (engine_) engine_->newGame();
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            board_.setFen(value);
        }
        headers_[key] = value;
    }

    void startMoves() override {}

    void move(std::string_view move, [[maybe_unused]] std::string_view comment) override {
        auto mv = chess::uci::parseSan(board_, move);
        board_.makeMove(mv);
        if (engine_) {
            pending_.push_back({++ply_, std::string(move), engine_->analyze(board_.getFen())});
        }
    }

    void endPgn() override {
        ++index_;
        if (engine_) drain();
    }

    std::size_t count() const { return index_; }

private:
    struct Pending {
        int ply;
        std::string san;
        std::future<uci::Analysis> future;
    };

    static std::string formatScore(const uci::PvLine& line) {
        if (line.mate) return "#" + std::to_string(*line.mate);
        if (line.cp) return std::to_string(*line.cp) + "cp";
        return "?";
    }

    void drain() {
        std::cout << "Game " << index_ << ":\n";
        for (auto& p : pending_) {
            uci::Analysis a = p.future.get();
            std::cout << "  " << p.ply << ". " << p.san << "  " << a.fen << "\n";
            std::cout << "    bestmove " << a.bestmove << "\n";
            for (const auto& line : a.lines) {
                std::cout << "    [" << line.multipv << "] " << formatScore(line)
                          << " d" << line.depth << " " << line.moves << "\n";
            }
        }
        pending_.clear();
    }

    uci::UciEngine* engine_;
    std::size_t index_ = 0;
    int ply_ = 0;
    std::unordered_map<std::string_view, std::string_view> headers_;
    std::deque<Pending> pending_;
    chess::Board board_;
};

int main(int argc, char* argv[]) {
    CLI::App app{"tictac - read a PGN file and process each game"};
    app.set_version_flag("--version", "tictac 0.1.0");

    std::string filename;
    app.add_option("-f,--file", filename, "PGN file to read")
        ->required()
        ->check(CLI::ExistingFile);

    std::string engine_path;
    app.add_option("--engine", engine_path, "Path to a UCI engine binary (enables evaluation)");

    std::vector<std::string> engine_args;
    app.add_option("--engine-arg", engine_args, "Extra argument passed to the engine (repeatable)");

    std::vector<std::string> engine_options;
    app.add_option("--engine-option", engine_options,
                   "UCI option as NAME=VALUE, sent via setoption (repeatable)");

    int multipv = 1;
    app.add_option("--multipv", multipv, "Number of best lines (UCI MultiPV)")->check(CLI::PositiveNumber);

    int depth = 0, movetime = 0, nodes = 0;
    auto* depth_opt = app.add_option("--eval-depth", depth, "Search to fixed depth")->check(CLI::PositiveNumber);
    auto* movetime_opt = app.add_option("--eval-movetime", movetime, "Search for fixed milliseconds")->check(CLI::PositiveNumber);
    auto* nodes_opt = app.add_option("--eval-nodes", nodes, "Search to fixed node count")->check(CLI::PositiveNumber);
    depth_opt->excludes(movetime_opt)->excludes(nodes_opt);
    movetime_opt->excludes(nodes_opt);

    CLI11_PARSE(app, argc, argv);

    std::ifstream file(filename);
    if (!file) {
        std::cerr << "error: cannot open file: " << filename << "\n";
        return 1;
    }

    std::unique_ptr<uci::UciEngine> engine;
    if (!engine_path.empty()) {
        uci::EngineConfig config;
        config.path = engine_path;
        config.args = engine_args;
        config.multipv = multipv;
        for (const auto& opt : engine_options) {
            auto eq = opt.find('=');
            if (eq == std::string::npos) {
                std::cerr << "error: --engine-option expects NAME=VALUE: " << opt << "\n";
                return 1;
            }
            config.options.emplace_back(opt.substr(0, eq), opt.substr(eq + 1));
        }

        uci::Limits limits;
        if (depth_opt->count()) limits.depth = depth;
        else if (movetime_opt->count()) limits.movetime_ms = movetime;
        else if (nodes_opt->count()) limits.nodes = nodes;

        try {
            engine = std::make_unique<uci::UciEngine>(std::move(config), limits);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }

    GameVisitor visitor(engine.get());
    chess::pgn::StreamParser parser(file);
    if (auto err = parser.readGames(visitor)) {
        std::cerr << "error: " << err.message() << "\n";
        return 1;
    }

    const std::size_t games = visitor.count();
    std::cout << "Processed " << games << " game" << (games == 1 ? "" : "s")
              << " from " << filename << "\n";
    return 0;
}
