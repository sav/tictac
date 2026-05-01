#include "viz/board_widget.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <array>
#include <memory>

namespace tictac {

namespace {

// Resource paths populated by `qrc_pieces` (built from src/viz/pieces.qrc).
QString svg_path_for(char piece) {
    switch (piece) {
        case 'K': return ":/pieces/merida/wK.svg";
        case 'Q': return ":/pieces/merida/wQ.svg";
        case 'R': return ":/pieces/merida/wR.svg";
        case 'B': return ":/pieces/merida/wB.svg";
        case 'N': return ":/pieces/merida/wN.svg";
        case 'P': return ":/pieces/merida/wP.svg";
        case 'k': return ":/pieces/merida/bK.svg";
        case 'q': return ":/pieces/merida/bQ.svg";
        case 'r': return ":/pieces/merida/bR.svg";
        case 'b': return ":/pieces/merida/bB.svg";
        case 'n': return ":/pieces/merida/bN.svg";
        case 'p': return ":/pieces/merida/bP.svg";
        default:  return {};
    }
}

// One renderer per piece type. Cached on first use; the resource lives for
// the lifetime of the binary so this is fine to keep as a function-local
// static.
QSvgRenderer* renderer_for(char piece) {
    static std::array<std::unique_ptr<QSvgRenderer>, 128> cache;
    auto idx = static_cast<unsigned char>(piece);
    if (!cache[idx]) {
        QString path = svg_path_for(piece);
        if (path.isEmpty()) return nullptr;
        cache[idx] = std::make_unique<QSvgRenderer>(path);
        if (!cache[idx]->isValid()) {
            cache[idx].reset();
            return nullptr;
        }
    }
    return cache[idx].get();
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
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const int side = qMin(width(), height());
    const int sq = side / 8;
    const int x0 = (width() - sq * 8) / 2;
    const int y0 = (height() - sq * 8) / 2;

    static const QColor light(240, 217, 181);
    static const QColor dark(181, 136, 99);

    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            QRect cell(x0 + f * sq, y0 + (7 - r) * sq, sq, sq);
            const bool is_light = ((f + r) % 2) == 0;
            p.fillRect(cell, is_light ? light : dark);
        }
    }

    if (!valid_) return;

    // Inset pieces a couple of pixels so they don't crowd the square edge.
    const int pad = qMax(1, sq / 18);
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            char piece = model_.at(f, r);
            if (!piece) continue;
            QSvgRenderer* renderer = renderer_for(piece);
            if (!renderer) continue;
            QRect cell(x0 + f * sq + pad, y0 + (7 - r) * sq + pad,
                       sq - 2 * pad, sq - 2 * pad);
            renderer->render(&p, cell);
        }
    }
}

} // namespace tictac
