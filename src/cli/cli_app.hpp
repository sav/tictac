#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tictac {

class CliApp {
public:
    int run(int argc, char* argv[]);

private:
    int cmd_import(const std::filesystem::path& input,
                   const std::filesystem::path& db_path,
                   unsigned threads);
    int cmd_search_position(const std::string& fen,
                            const std::filesystem::path& db_path,
                            std::size_t limit,
                            const std::filesystem::path& plugin_path);
    int cmd_search_opening(const std::vector<std::string>& moves,
                           const std::filesystem::path& db_path,
                           std::size_t limit,
                           const std::filesystem::path& plugin_path);
    int cmd_stats(const std::filesystem::path& db_path);
    int cmd_compact(const std::filesystem::path& db_path);
};

} // namespace tictac
