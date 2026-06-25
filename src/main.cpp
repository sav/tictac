#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <CLI/CLI.hpp>
#include <chess.hpp>

class GameVisitor : public chess::pgn::Visitor {
public:
    void startPgn() override {
        board_.setFen(chess::constants::STARTPOS);
		headers_.clear();
    }

    void header(std::string_view key, std::string_view value) override {
		if (key == "FEN") {
			board_.setFen(value);
		}
		headers_.insert_or_assign(std::string(key), std::string(value));
    }

    void startMoves() override {}

	void move(std::string_view move, [[maybe_unused]] std::string_view comment) override {
        auto mv = chess::uci::parseSan(board_, move);
        board_.makeMove(mv);
	}

    void endPgn() override {
        ++index_;
    }

    std::size_t count() const { return index_; }

private:
    std::size_t index_ = 0;
	std::unordered_map<std::string, std::string> headers_;
    chess::Board board_;
};

int main(int argc, char *argv[]) {
    CLI::App app{"tictac - read a PGN file and process each game"};
    app.set_version_flag("--version", "tictac 0.1.0");

    std::string filename;
    app.add_option("-f,--file", filename, "PGN file to read")
        ->required()
        ->check(CLI::ExistingFile);

    CLI11_PARSE(app, argc, argv);

    std::ifstream file(filename);
    if (!file) {
        std::cerr << "error: cannot open file: " << filename << "\n";
        return 1;
    }

    GameVisitor visitor;
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
