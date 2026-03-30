#pragma once

#include <functional>
#include <string_view>

#include "chess.hpp"
#include "core/types.hpp"

namespace tictac {

/// Callback invoked with each fully-parsed game.
using GameCallback = std::function<void(GameRecord&&)>;

/// chess::pgn::Visitor subclass that builds GameRecord objects.
class ImportVisitor : public chess::pgn::Visitor {
public:
    explicit ImportVisitor(GameCallback callback);

    void startPgn() override;
    void header(std::string_view key, std::string_view value) override;
    void startMoves() override;
    void move(std::string_view san, std::string_view comment) override;
    void endPgn() override;

    [[nodiscard]] GameId games_parsed() const { return next_id_; }
    [[nodiscard]] std::uint64_t parse_errors() const { return parse_errors_; }

private:
    GameCallback callback_;
    GameId       next_id_      = 0;
    std::uint64_t parse_errors_ = 0;
    GameRecord   current_;
    chess::Board board_;
    bool         has_error_ = false;
};

} // namespace tictac
