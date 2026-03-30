#include "storage/sequence_index.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace tictac {

SequenceIndex::SequenceIndex(const std::filesystem::path& db_path, unsigned max_depth)
    : db_path_(db_path), max_depth_(max_depth)
{
    std::filesystem::create_directories(db_path_);
    // Root node
    nodes_.emplace_back();
}

void SequenceIndex::insert(GameId game_id, const std::vector<CompactMove>& moves) {
    std::lock_guard lock(mutex_);

    std::uint32_t node_idx = 0; // root
    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);

    // Root always gets the game
    nodes_[0].game_ids.push_back(game_id);

    for (unsigned i = 0; i < depth; ++i) {
        auto move = moves[i];
        auto it = nodes_[node_idx].children.find(move);
        if (it == nodes_[node_idx].children.end()) {
            auto new_idx = static_cast<std::uint32_t>(nodes_.size());
            nodes_.emplace_back();
            nodes_[node_idx].children[move] = new_idx;
            node_idx = new_idx;
        } else {
            node_idx = it->second;
        }
        nodes_[node_idx].game_ids.push_back(game_id);
    }
}

std::vector<GameId> SequenceIndex::search_prefix(const std::vector<CompactMove>& moves) const {
    std::lock_guard lock(mutex_);
    auto idx = walk(moves);
    if (idx < 0) return {};
    return nodes_[static_cast<std::size_t>(idx)].game_ids;
}

std::uint32_t SequenceIndex::count(const std::vector<CompactMove>& moves) const {
    std::lock_guard lock(mutex_);
    auto idx = walk(moves);
    if (idx < 0) return 0;
    return static_cast<std::uint32_t>(nodes_[static_cast<std::size_t>(idx)].game_ids.size());
}

std::int64_t SequenceIndex::walk(const std::vector<CompactMove>& moves) const {
    std::uint32_t node_idx = 0;
    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);

    for (unsigned i = 0; i < depth; ++i) {
        auto it = nodes_[node_idx].children.find(moves[i]);
        if (it == nodes_[node_idx].children.end())
            return -1;
        node_idx = it->second;
    }

    return static_cast<std::int64_t>(node_idx);
}

std::uint32_t SequenceIndex::walk_or_create(const std::vector<CompactMove>& moves) {
    std::uint32_t node_idx = 0;
    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);

    for (unsigned i = 0; i < depth; ++i) {
        auto move = moves[i];
        auto it = nodes_[node_idx].children.find(move);
        if (it == nodes_[node_idx].children.end()) {
            auto new_idx = static_cast<std::uint32_t>(nodes_.size());
            nodes_.emplace_back();
            nodes_[node_idx].children[move] = new_idx;
            node_idx = new_idx;
        } else {
            node_idx = it->second;
        }
    }
    return node_idx;
}

void SequenceIndex::save() const {
    std::lock_guard lock(mutex_);
    auto path = db_path_ / "sequence_trie.dat";

    std::ofstream out(path, std::ios::binary);

    // Header: node count
    auto node_count = static_cast<std::uint32_t>(nodes_.size());
    out.write(reinterpret_cast<const char*>(&node_count), 4);

    for (const auto& node : nodes_) {
        // Children count + entries
        auto child_count = static_cast<std::uint32_t>(node.children.size());
        out.write(reinterpret_cast<const char*>(&child_count), 4);
        for (const auto& [move, idx] : node.children) {
            out.write(reinterpret_cast<const char*>(&move), 2);
            out.write(reinterpret_cast<const char*>(&idx), 4);
        }

        // Game IDs count + data
        auto game_count = static_cast<std::uint32_t>(node.game_ids.size());
        out.write(reinterpret_cast<const char*>(&game_count), 4);
        out.write(reinterpret_cast<const char*>(node.game_ids.data()),
                  static_cast<std::streamsize>(game_count * sizeof(GameId)));
    }
}

void SequenceIndex::load() {
    std::lock_guard lock(mutex_);
    auto path = db_path_ / "sequence_trie.dat";

    if (!std::filesystem::exists(path)) return;

    std::ifstream in(path, std::ios::binary);

    std::uint32_t node_count = 0;
    in.read(reinterpret_cast<char*>(&node_count), 4);

    nodes_.clear();
    nodes_.resize(node_count);

    for (std::uint32_t n = 0; n < node_count; ++n) {
        std::uint32_t child_count = 0;
        in.read(reinterpret_cast<char*>(&child_count), 4);
        for (std::uint32_t c = 0; c < child_count; ++c) {
            CompactMove move = 0;
            std::uint32_t idx = 0;
            in.read(reinterpret_cast<char*>(&move), 2);
            in.read(reinterpret_cast<char*>(&idx), 4);
            nodes_[n].children[move] = idx;
        }

        std::uint32_t game_count = 0;
        in.read(reinterpret_cast<char*>(&game_count), 4);
        nodes_[n].game_ids.resize(game_count);
        in.read(reinterpret_cast<char*>(nodes_[n].game_ids.data()),
                static_cast<std::streamsize>(game_count * sizeof(GameId)));
    }
}

} // namespace tictac
