#include "storage/db_manifest.hpp"

#include <fstream>
#include <sstream>

namespace tictac {

DbManifest::DbManifest(const std::filesystem::path& db_path) {
    std::filesystem::create_directories(db_path);
    manifest_path_ = db_path / "manifest.txt";
    load();
}

bool DbManifest::is_file_indexed(const std::filesystem::path& pgn_path) const {
    auto canonical = std::filesystem::canonical(pgn_path).string();
    auto fsize = std::filesystem::file_size(pgn_path);

    for (const auto& e : entries_) {
        if (e.canonical_path == canonical && e.file_size == fsize)
            return true;
    }
    return false;
}

void DbManifest::mark_file_indexed(const std::filesystem::path& pgn_path,
                                   GameId first_id, GameId last_id) {
    FileEntry entry;
    entry.canonical_path = std::filesystem::canonical(pgn_path).string();
    entry.file_size = std::filesystem::file_size(pgn_path);
    entry.first_id = first_id;
    entry.last_id  = last_id;
    entries_.push_back(std::move(entry));
}

GameId DbManifest::next_game_id() const {
    GameId max_id = 0;
    for (const auto& e : entries_) {
        if (e.last_id > max_id)
            max_id = e.last_id;
    }
    return entries_.empty() ? 0 : max_id + 1;
}

void DbManifest::save() const {
    std::ofstream out(manifest_path_);
    for (const auto& e : entries_) {
        out << e.canonical_path << "\t"
            << e.file_size << "\t"
            << e.first_id << "\t"
            << e.last_id << "\n";
    }
}

void DbManifest::load() {
    entries_.clear();
    if (!std::filesystem::exists(manifest_path_)) return;

    std::ifstream in(manifest_path_);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        FileEntry entry;
        iss >> entry.canonical_path >> entry.file_size >> entry.first_id >> entry.last_id;
        if (!iss.fail())
            entries_.push_back(std::move(entry));
    }
}

} // namespace tictac
