#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <map>
#include <string>
#include <vector>

namespace tictac {

class BoardWidget;

struct VizEntry {
    std::string fen;
    std::map<std::string, std::string> info;
};

/// Browser for a buffered list of (FEN, info) entries. Right pane shows the
/// board, left pane shows free-form key/value info, bottom row has Prev /
/// Next / Close. Navigation wraps neither end — buttons disable on bounds.
class BoardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit BoardWindow(std::vector<VizEntry> entries, QWidget* parent = nullptr);

private:
    void show_index(std::size_t i);
    void rebuild_info_panel();

    std::vector<VizEntry> entries_;
    std::size_t index_ = 0;

    BoardWidget* board_ = nullptr;
    QLabel* info_label_ = nullptr;
    QLabel* counter_label_ = nullptr;
    QPushButton* prev_btn_ = nullptr;
    QPushButton* next_btn_ = nullptr;
};

} // namespace tictac
