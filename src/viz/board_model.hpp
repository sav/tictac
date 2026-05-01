#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace tictac {

/// Pure parser/representation of the piece-placement field of a FEN. Kept
/// separate from the Qt widget so it can be unit-tested without a display.
class BoardModel {
public:
    BoardModel() = default;

    /// Parse the full FEN's first field. Throws std::invalid_argument on
    /// malformed input. Anything past the first space is ignored.
    static BoardModel from_fen(std::string_view fen);

    /// 0 = empty; otherwise an ASCII letter — uppercase for white, lowercase
    /// for black ('K','Q','R','B','N','P','k','q','r','b','n','p'). Index 0
    /// is a8, 7 is h8, 56 is a1, 63 is h1 (rank 8 first, then down).
    char at(int file, int rank) const {
        return squares_[index_of(file, rank)];
    }

    bool empty(int file, int rank) const { return at(file, rank) == 0; }

private:
    static constexpr std::size_t index_of(int file, int rank) {
        return static_cast<std::size_t>((7 - rank) * 8 + file);
    }

    std::array<char, 64> squares_{};
};

} // namespace tictac
