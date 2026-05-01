#pragma once

#include <QWidget>
#include <string>

#include "viz/board_model.hpp"

namespace tictac {

/// Paints a chess board from a FEN string. Pieces are drawn as Unicode
/// glyphs (♔♕♖♗♘♙ / ♚♛♜♝♞♟) so no image assets are required.
class BoardWidget : public QWidget {
    Q_OBJECT

public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void set_fen(const std::string& fen);

    QSize sizeHint() const override { return {480, 480}; }
    QSize minimumSizeHint() const override { return {240, 240}; }

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    BoardModel model_;
    std::string fen_;
    bool valid_ = false;
};

} // namespace tictac
