#include "viz/board_widget.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace tictac {

namespace {

const QChar* glyph_for(char piece) {
    static const QChar wK{0x2654}, wQ{0x2655}, wR{0x2656}, wB{0x2657}, wN{0x2658}, wP{0x2659};
    static const QChar bK{0x265A}, bQ{0x265B}, bR{0x265C}, bB{0x265D}, bN{0x265E}, bP{0x265F};
    switch (piece) {
        case 'K': return &wK; case 'Q': return &wQ; case 'R': return &wR;
        case 'B': return &wB; case 'N': return &wN; case 'P': return &wP;
        case 'k': return &bK; case 'q': return &bQ; case 'r': return &bR;
        case 'b': return &bB; case 'n': return &bN; case 'p': return &bP;
        default: return nullptr;
    }
}

} // namespace

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(minimumSizeHint());
}

void BoardWidget::set_fen(const std::string& fen) {
    fen_ = fen;
    try {
        model_ = BoardModel::from_fen(fen);
        valid_ = true;
    } catch (...) {
        valid_ = false;
    }
    update();
}

void BoardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const int side = qMin(width(), height());
    const int sq = side / 8;
    const int x0 = (width() - sq * 8) / 2;
    const int y0 = (height() - sq * 8) / 2;

    static const QColor light(240, 217, 181);
    static const QColor dark(181, 136, 99);

    // Squares
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            QRect cell(x0 + f * sq, y0 + (7 - r) * sq, sq, sq);
            const bool is_light = ((f + r) % 2) == 0;
            p.fillRect(cell, is_light ? light : dark);
        }
    }

    if (!valid_) return;

    QFont font = p.font();
    font.setPixelSize(static_cast<int>(sq * 0.78));
    p.setFont(font);

    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            char piece = model_.at(f, r);
            if (!piece) continue;
            const QChar* g = glyph_for(piece);
            if (!g) continue;
            QRect cell(x0 + f * sq, y0 + (7 - r) * sq, sq, sq);
            // Outline both for readability against either square color.
            p.setPen(Qt::black);
            p.drawText(cell, Qt::AlignCenter, QString(*g));
        }
    }
}

} // namespace tictac
