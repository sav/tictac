#include "viz/board_model.hpp"

#include <cctype>

namespace tictac {

BoardModel BoardModel::from_fen(std::string_view fen) {
    BoardModel m;
    int file = 0;
    int rank = 7;
    std::size_t i = 0;
    for (; i < fen.size() && fen[i] != ' '; ++i) {
        char c = fen[i];
        if (c == '/') {
            if (file != 8) throw std::invalid_argument("FEN: rank length != 8");
            file = 0;
            --rank;
            if (rank < 0) throw std::invalid_argument("FEN: too many ranks");
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) throw std::invalid_argument("FEN: rank overflow");
            continue;
        }
        switch (c) {
            case 'K': case 'Q': case 'R': case 'B': case 'N': case 'P':
            case 'k': case 'q': case 'r': case 'b': case 'n': case 'p':
                if (file >= 8 || rank < 0)
                    throw std::invalid_argument("FEN: piece past end of rank");
                m.squares_[index_of(file, rank)] = c;
                ++file;
                break;
            default:
                throw std::invalid_argument("FEN: unexpected character");
        }
    }
    if (rank != 0 || file != 8)
        throw std::invalid_argument("FEN: incomplete board");
    return m;
}

} // namespace tictac
