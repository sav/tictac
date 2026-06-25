#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tictac {

struct ImageOptions {
    int size = 512;
    std::string format = "png";
    bool flip = false;
    bool coordinates = true;
    std::optional<std::pair<std::string, std::string>> lastMove;
    std::vector<std::string> highlight;
    std::vector<std::pair<std::string, std::string>> arrows;
    std::string theme = "default";
};

// Render the position given by `fen` to `path`. Returns the output path on
// success, throws std::runtime_error on failure.
std::string renderImage(const std::string &fen, const std::string &path, const ImageOptions &opts);

} // namespace tictac
