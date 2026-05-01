#include "viz/board_window.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

#include "viz/board_widget.hpp"

namespace tictac {

namespace {

QString to_qs(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

QString render_info(const std::map<std::string, std::string>& info) {
    // Stable, predictable ordering: known keys first, then anything extra
    // alphabetical. The plugin chooses what to populate; we just display it.
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
    setWindowTitle("tictac — board browser");
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
    prev_btn_    = new QPushButton("\xe2\x97\x80 Prev", central);
    next_btn_    = new QPushButton("Next \xe2\x96\xb6", central);
    counter_label_ = new QLabel(central);
    counter_label_->setAlignment(Qt::AlignCenter);
    auto* close_btn = new QPushButton("Close", central);
    btn_row->addWidget(prev_btn_);
    btn_row->addStretch(1);
    btn_row->addWidget(counter_label_);
    btn_row->addStretch(1);
    btn_row->addWidget(next_btn_);
    btn_row->addSpacing(16);
    btn_row->addWidget(close_btn);
    right_col->addLayout(btn_row);

    hbox->addWidget(info_label_, 0);
    hbox->addLayout(right_col, 1);
    setCentralWidget(central);

    QObject::connect(prev_btn_, &QPushButton::clicked, this, [this]() {
        if (index_ > 0) show_index(index_ - 1);
    });
    QObject::connect(next_btn_, &QPushButton::clicked, this, [this]() {
        if (index_ + 1 < entries_.size()) show_index(index_ + 1);
    });
    QObject::connect(close_btn, &QPushButton::clicked, this, &QMainWindow::close);

    if (!entries_.empty()) show_index(0);
    else {
        info_label_->setText("<i>no games to display</i>");
        prev_btn_->setEnabled(false);
        next_btn_->setEnabled(false);
        counter_label_->setText("0 / 0");
    }
}

void BoardWindow::show_index(std::size_t i) {
    if (i >= entries_.size()) return;
    index_ = i;
    const auto& e = entries_[i];
    board_->set_fen(e.fen);
    info_label_->setText(render_info(e.info));
    counter_label_->setText(QString("%1 / %2")
                                .arg(static_cast<qulonglong>(i + 1))
                                .arg(static_cast<qulonglong>(entries_.size())));
    prev_btn_->setEnabled(i > 0);
    next_btn_->setEnabled(i + 1 < entries_.size());
}

void BoardWindow::rebuild_info_panel() {}

} // namespace tictac
