#include "image.hpp"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tictac {

namespace {

const char *pieceGlyph(char p) {
    switch (p) {
    case 'K': return "♔";
    case 'Q': return "♕";
    case 'R': return "♖";
    case 'B': return "♗";
    case 'N': return "♘";
    case 'P': return "♙";
    case 'k': return "♚";
    case 'q': return "♛";
    case 'r': return "♜";
    case 'b': return "♝";
    case 'n': return "♞";
    case 'p': return "♟";
    default: return "";
    }
}

// Parse the piece-placement field of a FEN into an 8x8 grid, rank 8 first.
std::array<std::array<char, 8>, 8> parsePlacement(const std::string &fen) {
    std::array<std::array<char, 8>, 8> grid{};
    for (auto &row : grid) row.fill(' ');
    int rank = 0, file = 0;
    for (char c : fen) {
        if (c == ' ') break;
        if (c == '/') {
            ++rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (rank < 8 && file < 8) {
            grid[rank][file] = c;
            ++file;
        }
    }
    return grid;
}

// Pixel centre of a square, honouring board flip. file/rank are 0-7.
std::pair<double, double> squareCenter(int file, int rank, double cell, double margin, bool flip) {
    int col = flip ? 7 - file : file;
    int row = flip ? rank : 7 - rank; // rank 0 = rank1 at bottom
    return {margin + (col + 0.5) * cell, margin + (row + 0.5) * cell};
}

std::pair<int, int> squareToFileRank(const std::string &sq) {
    if (sq.size() < 2) return {-1, -1};
    return {sq[0] - 'a', sq[1] - '1'};
}

} // namespace

std::string renderImage(const std::string &fen, const std::string &path, const ImageOptions &opts) {
    if (opts.format != "svg") {
        throw std::runtime_error("image: only the 'svg' format is supported in v1 (got '" + opts.format + "')");
    }

    const double margin = opts.coordinates ? opts.size * 0.05 : 0.0;
    const double boardSize = opts.size - 2 * margin;
    const double cell = boardSize / 8.0;
    const auto grid = parsePlacement(fen);

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << opts.size << "\" height=\"" << opts.size
        << "\" viewBox=\"0 0 " << opts.size << " " << opts.size << "\">\n";
    svg << "<defs><marker id=\"arrow\" markerWidth=\"4\" markerHeight=\"4\" refX=\"2\" refY=\"2\" orient=\"auto\">"
        << "<path d=\"M0,0 L4,2 L0,4 z\" fill=\"#15781b\"/></marker></defs>\n";

    const char *light = "#f0d9b5";
    const char *dark = "#b58863";
    const char *hl = "#aaccff";
    const char *lastHl = "#cdd26a";

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            int col = opts.flip ? 7 - file : file;
            int row = opts.flip ? rank : 7 - rank;
            double x = margin + col * cell;
            double y = margin + row * cell;
            bool lightSq = (file + rank) % 2 != 0;
            svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << cell << "\" height=\"" << cell
                << "\" fill=\"" << (lightSq ? light : dark) << "\"/>\n";
        }
    }

    const auto paintSquare = [&](const std::string &sq, const char *color) {
        auto [f, r] = squareToFileRank(sq);
        if (f < 0 || r < 0) return;
        int col = opts.flip ? 7 - f : f;
        int row = opts.flip ? r : 7 - r;
        double x = margin + col * cell;
        double y = margin + row * cell;
        svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << cell << "\" height=\"" << cell << "\" fill=\""
            << color << "\" fill-opacity=\"0.55\"/>\n";
    };

    if (opts.lastMove) {
        paintSquare(opts.lastMove->first, lastHl);
        paintSquare(opts.lastMove->second, lastHl);
    }
    for (const auto &sq : opts.highlight) paintSquare(sq, hl);

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            char p = grid[7 - rank][file];
            const char *glyph = pieceGlyph(p);
            if (!*glyph) continue;
            auto [cx, cy] = squareCenter(file, rank, cell, margin, opts.flip);
            svg << "<text x=\"" << cx << "\" y=\"" << (cy + cell * 0.33) << "\" font-size=\"" << (cell * 0.85)
                << "\" text-anchor=\"middle\" font-family=\"sans-serif\">" << glyph << "</text>\n";
        }
    }

    for (const auto &[from, to] : opts.arrows) {
        auto [ff, fr] = squareToFileRank(from);
        auto [tf, tr] = squareToFileRank(to);
        if (ff < 0 || tf < 0) continue;
        auto [x1, y1] = squareCenter(ff, fr, cell, margin, opts.flip);
        auto [x2, y2] = squareCenter(tf, tr, cell, margin, opts.flip);
        svg << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2
            << "\" stroke=\"#15781b\" stroke-width=\"" << (cell * 0.18)
            << "\" stroke-opacity=\"0.7\" marker-end=\"url(#arrow)\"/>\n";
    }

    if (opts.coordinates) {
        for (int i = 0; i < 8; ++i) {
            char fileLabel = static_cast<char>('a' + (opts.flip ? 7 - i : i));
            char rankLabel = static_cast<char>('1' + (opts.flip ? i : 7 - i));
            double fx = margin + i * cell + cell * 0.5;
            double fy = opts.size - margin * 0.25;
            svg << "<text x=\"" << fx << "\" y=\"" << fy << "\" font-size=\"" << (margin * 0.7)
                << "\" text-anchor=\"middle\" font-family=\"sans-serif\">" << fileLabel << "</text>\n";
            double rx = margin * 0.4;
            double ry = margin + i * cell + cell * 0.5;
            svg << "<text x=\"" << rx << "\" y=\"" << ry << "\" font-size=\"" << (margin * 0.7)
                << "\" text-anchor=\"middle\" font-family=\"sans-serif\">" << rankLabel << "</text>\n";
        }
    }

    svg << "</svg>\n";

    std::ofstream file(path);
    if (!file) throw std::runtime_error("image: cannot open output file: " + path);
    file << svg.str();
    return path;
}

} // namespace tictac
