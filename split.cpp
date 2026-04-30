// split.cpp -- split a PGN file into N parts (default 2).
//
// Build: g++ -std=c++17 -O2 -o split split.cpp
// Usage: ./split <pgn-file> [N]
//
// Output files are named <basename>_part1.pgn, <basename>_part2.pgn, ...
// Games are distributed as evenly as possible; if N exceeds the number of
// games, fewer files are produced.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <pgn-file> [N]\n";
        return 1;
    }

    int n = 2;
    if (argc == 3) {
        n = std::atoi(argv[2]);
        if (n <= 0) {
            std::cerr << "N must be a positive integer\n";
            return 1;
        }
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "Cannot open " << argv[1] << "\n";
        return 1;
    }

    // A PGN game = tag-pair section + blank line + movetext + blank line.
    // We detect the start of a new game as a '[' line that appears after we
    // have already seen movetext for the current game.
    std::vector<std::string> games;
    std::string current;
    std::string line;
    bool in_movetext = false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const bool is_tag = !line.empty() && line.front() == '[';
        const bool is_blank = line.empty();

        if (is_tag && in_movetext) {
            games.push_back(current);
            current.clear();
            in_movetext = false;
        }

        if (!is_tag && !is_blank) in_movetext = true;

        current += line;
        current += '\n';
    }
    if (!current.empty()) games.push_back(current);

    if (games.empty()) {
        std::cerr << "No games found in " << argv[1] << "\n";
        return 1;
    }

    if (n > static_cast<int>(games.size())) n = static_cast<int>(games.size());

    // Derive base name from input path (strip directory and .pgn extension).
    std::string base = argv[1];
    if (auto s = base.find_last_of("/\\"); s != std::string::npos)
        base = base.substr(s + 1);
    if (auto d = base.find_last_of('.'); d != std::string::npos)
        base = base.substr(0, d);

    const size_t total = games.size();
    const size_t per = total / n;
    const size_t extra = total % n;

    size_t idx = 0;
    for (int i = 0; i < n; ++i) {
        const size_t count = per + (static_cast<size_t>(i) < extra ? 1 : 0);

        std::ostringstream fname;
        fname << base << "_part" << (i + 1) << ".pgn";

        std::ofstream out(fname.str());
        if (!out) {
            std::cerr << "Cannot write " << fname.str() << "\n";
            return 1;
        }
        for (size_t j = 0; j < count; ++j) out << games[idx++];

        std::cout << "Wrote " << fname.str() << " (" << count << " games)\n";
    }

    return 0;
}
