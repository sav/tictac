#include "viz/board_window.hpp"

#include <QHBoxLayout>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWidget>

#include "viz/board_widget.hpp"

namespace tictac {

namespace {

QString to_qs(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

QString render_info(const std::map<std::string, std::string>& info) {
    static const char* known[] = {"id", "white", "black", "white_elo",
                                  "black_elo", "result", "event", "date",
                                  "move_count"};
    QString s;
    auto append = [&](const std::string& k, const std::string& v) {
        s += "<b>" + to_qs(k) + ":</b> " + to_qs(v) + "<br>";
    };
    for (const char* k : known) {
        auto it = info.find(k);
        if (it != info.end() && !it->second.empty()) append(it->first, it->second);
    }
    for (const auto& [k, v] : info) {
        bool is_known = false;
        for (const char* kk : known) if (k == kk) { is_known = true; break; }
        if (!is_known && !v.empty()) append(k, v);
    }
    return s;
}

} // namespace

BoardWindow::BoardWindow(std::vector<VizEntry> entries, QWidget* parent)
    : QMainWindow(parent), entries_(std::move(entries)) {
    setWindowTitle("tictac \xe2\x80\x94 board browser");
    resize(900, 560);

    auto* central = new QWidget(this);
    auto* hbox = new QHBoxLayout(central);
    hbox->setContentsMargins(8, 8, 8, 8);
    hbox->setSpacing(8);

    info_label_ = new QLabel(central);
    info_label_->setTextFormat(Qt::RichText);
    info_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    info_label_->setMinimumWidth(260);
    info_label_->setWordWrap(true);

    auto* right_col = new QVBoxLayout();
    board_ = new BoardWidget(central);
    right_col->addWidget(board_, 1);

    auto* btn_row = new QHBoxLayout();
    prev_btn_      = new QPushButton("\xe2\x97\x80 Prev",   central);
    next_btn_      = new QPushButton("Next \xe2\x96\xb6",   central);
    next_game_btn_ = new QPushButton("Next Game \xe2\x8f\xa9", central);
    counter_label_ = new QLabel(central);
    counter_label_->setAlignment(Qt::AlignCenter);

    btn_row->addWidget(prev_btn_);
    btn_row->addWidget(next_btn_);
    btn_row->addStretch(1);
    btn_row->addWidget(counter_label_);
    btn_row->addStretch(1);
    btn_row->addWidget(next_game_btn_);
    right_col->addLayout(btn_row);

    hbox->addWidget(info_label_, 0);
    hbox->addLayout(right_col, 1);
    setCentralWidget(central);

    QObject::connect(prev_btn_,      &QPushButton::clicked, this, &BoardWindow::go_prev_ply);
    QObject::connect(next_btn_,      &QPushButton::clicked, this, &BoardWindow::go_next_ply);
    QObject::connect(next_game_btn_, &QPushButton::clicked, this, &BoardWindow::go_next_game);

    // Arrow keys mirror the buttons. PgDn jumps to the next game.
    auto* sc_left  = new QShortcut(QKeySequence(Qt::Key_Left),     this);
    auto* sc_right = new QShortcut(QKeySequence(Qt::Key_Right),    this);
    auto* sc_down  = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    QObject::connect(sc_left,  &QShortcut::activated, this, &BoardWindow::go_prev_ply);
    QObject::connect(sc_right, &QShortcut::activated, this, &BoardWindow::go_next_ply);
    QObject::connect(sc_down,  &QShortcut::activated, this, &BoardWindow::go_next_game);

    if (!entries_.empty()) {
        show_state();
    } else {
        info_label_->setText("<i>no games to display</i>");
        counter_label_->setText("0 / 0");
        prev_btn_->setEnabled(false);
        next_btn_->setEnabled(false);
        next_game_btn_->setEnabled(false);
    }
}

void BoardWindow::go_prev_ply() {
    if (ply_ == 0) return;
    --ply_;
    show_state();
}

void BoardWindow::go_next_ply() {
    if (entries_.empty()) return;
    const auto& cur = entries_[entry_];
    if (ply_ + 1 >= cur.fens.size()) return;
    ++ply_;
    show_state();
}

void BoardWindow::go_next_game() {
    if (entry_ + 1 >= entries_.size()) return;
    ++entry_;
    ply_ = 0;
    show_state();
}

void BoardWindow::show_state() {
    const auto& e = entries_[entry_];
    if (e.fens.empty()) return;
    if (ply_ >= e.fens.size()) ply_ = e.fens.size() - 1;
    board_->set_fen(e.fens[ply_]);
    info_label_->setText(render_info(e.info));

    const std::size_t plies = e.fens.size() - 1;
    counter_label_->setText(QString("game %1 / %2 \xc2\xb7 ply %3 / %4")
                                .arg(static_cast<qulonglong>(entry_ + 1))
                                .arg(static_cast<qulonglong>(entries_.size()))
                                .arg(static_cast<qulonglong>(ply_))
                                .arg(static_cast<qulonglong>(plies)));

    prev_btn_->setEnabled(ply_ > 0);
    next_btn_->setEnabled(ply_ + 1 < e.fens.size());
    next_game_btn_->setEnabled(entry_ + 1 < entries_.size());
}

} // namespace tictac
