#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <map>
#include <string>
#include <vector>

namespace tictac {

class BoardWidget;

/// One game (or one static position) queued for the browser. `fens` holds
/// the position after every half-move played: `fens[0]` is the starting
/// position and `fens[N]` is the position after the N-th half-move. A
/// single-FEN entry with no playable moves is represented by `fens` of
/// length 1.
struct VizEntry {
    std::vector<std::string> fens;
    std::map<std::string, std::string> info;
};

/// Browser for buffered VizEntry list.
///
/// Three buttons:
///   * Prev / Next     — step backward / forward through plies inside the
///                       current entry.
///   * Next Game       — advance to the next entry, jumping to its first
///                       ply.
///
/// All three disable on bounds.
class BoardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit BoardWindow(std::vector<VizEntry> entries, QWidget* parent = nullptr);

private:
    void show_state();
    void go_prev_ply();
    void go_next_ply();
    void go_next_game();

    std::vector<VizEntry> entries_;
    std::size_t entry_ = 0;
    std::size_t ply_   = 0;

    BoardWidget* board_ = nullptr;
    QLabel* info_label_ = nullptr;
    QLabel* counter_label_ = nullptr;
    QPushButton* prev_btn_ = nullptr;
    QPushButton* next_btn_ = nullptr;
    QPushButton* next_game_btn_ = nullptr;
};

} // namespace tictac
