#include "storage/game_store.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace tictac {

GameStore::GameStore(const std::filesystem::path& db_path) {
    namespace fs = std::filesystem;
    fs::create_directories(db_path);

    data_path_  = db_path / "games.dat";
    index_path_ = db_path / "games.idx";

    // Open/create data file for appending
    data_fd_ = ::open(data_path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (data_fd_ < 0)
        throw std::runtime_error("GameStore: cannot open " + data_path_.string() + ": " + std::strerror(errno));

    // Load existing offset table
    if (fs::exists(index_path_) && fs::file_size(index_path_) > 0) {
        std::ifstream idx(index_path_, std::ios::binary);
        auto size = fs::file_size(index_path_);
        auto count = size / sizeof(std::uint64_t);
        offsets_.resize(count);
        idx.read(reinterpret_cast<char*>(offsets_.data()),
                 static_cast<std::streamsize>(size));
    }
}

GameId GameStore::append(const GameRecord& game) {
    std::lock_guard lock(write_mutex_);

    auto blob = serialize(game);

    // Current end of data file = offset for this game
    struct stat st{};
    ::fstat(data_fd_, &st);
    auto offset = static_cast<std::uint64_t>(st.st_size);

    // Write blob size (uint32) + blob
    auto blob_size = static_cast<std::uint32_t>(blob.size());
    std::byte size_buf[4];
    std::memcpy(size_buf, &blob_size, 4);

    ::lseek(data_fd_, 0, SEEK_END);
    ::write(data_fd_, size_buf, 4);
    ::write(data_fd_, blob.data(), blob.size());

    // Record offset
    auto id = static_cast<GameId>(offsets_.size());
    offsets_.push_back(offset);

    // Append offset to index file
    std::ofstream idx(index_path_, std::ios::binary | std::ios::app);
    idx.write(reinterpret_cast<const char*>(&offset), sizeof(offset));

    return id;
}

GameRecord GameStore::load(GameId id) const {
    if (id >= offsets_.size())
        throw std::runtime_error("GameStore: invalid GameId " + std::to_string(id));

    auto offset = offsets_[id];

    // Read blob size
    std::uint32_t blob_size = 0;
    ::pread(data_fd_, &blob_size, 4, static_cast<off_t>(offset));

    // Read blob
    std::vector<std::byte> blob(blob_size);
    ::pread(data_fd_, blob.data(), blob_size, static_cast<off_t>(offset + 4));

    auto record = deserialize(blob.data(), blob.size());
    record.id = id;
    return record;
}

GameId GameStore::count() const {
    return static_cast<GameId>(offsets_.size());
}

std::vector<std::byte> GameStore::serialize(const GameRecord& game) {
    std::vector<std::byte> buf;
    buf.reserve(256);

    write_string(buf, game.header.white);
    write_string(buf, game.header.black);
    write_string(buf, game.header.event);
    write_string(buf, game.header.date);

    // Result as uint8
    auto result = static_cast<std::uint8_t>(game.header.result);
    buf.push_back(static_cast<std::byte>(result));

    // Elos
    auto we = game.header.white_elo;
    auto be = game.header.black_elo;
    buf.insert(buf.end(), reinterpret_cast<const std::byte*>(&we),
               reinterpret_cast<const std::byte*>(&we) + 2);
    buf.insert(buf.end(), reinterpret_cast<const std::byte*>(&be),
               reinterpret_cast<const std::byte*>(&be) + 2);

    // Move count + moves
    auto move_count = static_cast<std::uint16_t>(game.moves.size());
    buf.insert(buf.end(), reinterpret_cast<const std::byte*>(&move_count),
               reinterpret_cast<const std::byte*>(&move_count) + 2);
    for (auto m : game.moves) {
        buf.insert(buf.end(), reinterpret_cast<const std::byte*>(&m),
                   reinterpret_cast<const std::byte*>(&m) + 2);
    }

    return buf;
}

GameRecord GameStore::deserialize(const std::byte* data, std::size_t len) {
    GameRecord game;
    std::size_t pos = 0;

    game.header.white = read_string(data, pos);
    game.header.black = read_string(data, pos);
    game.header.event = read_string(data, pos);
    game.header.date  = read_string(data, pos);

    if (pos >= len) throw std::runtime_error("GameStore: truncated record");

    game.header.result = static_cast<chess::GameResult>(
        static_cast<std::uint8_t>(data[pos++]));

    std::memcpy(&game.header.white_elo, data + pos, 2); pos += 2;
    std::memcpy(&game.header.black_elo, data + pos, 2); pos += 2;

    std::uint16_t move_count = 0;
    std::memcpy(&move_count, data + pos, 2); pos += 2;

    game.moves.resize(move_count);
    for (std::uint16_t i = 0; i < move_count; ++i) {
        std::memcpy(&game.moves[i], data + pos, 2);
        pos += 2;
    }

    return game;
}

void GameStore::write_string(std::vector<std::byte>& buf, const std::string& s) {
    auto len = static_cast<std::uint16_t>(s.size());
    buf.insert(buf.end(), reinterpret_cast<const std::byte*>(&len),
               reinterpret_cast<const std::byte*>(&len) + 2);
    buf.insert(buf.end(), reinterpret_cast<const std::byte*>(s.data()),
               reinterpret_cast<const std::byte*>(s.data()) + s.size());
}

std::string GameStore::read_string(const std::byte* data, std::size_t& pos) {
    std::uint16_t len = 0;
    std::memcpy(&len, data + pos, 2);
    pos += 2;
    std::string result(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return result;
}

} // namespace tictac
