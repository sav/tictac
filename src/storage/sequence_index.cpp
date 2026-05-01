#include "storage/sequence_index.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace tictac {

namespace {

void exec_or_throw(sqlite3* db, const char* sql, const char* what) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "SequenceIndex: ";
        msg += what;
        msg += ": ";
        if (err) { msg += err; sqlite3_free(err); }
        throw std::runtime_error(msg);
    }
}

void prepare_or_throw(sqlite3* db, const char* sql, sqlite3_stmt** out) {
    int rc = sqlite3_prepare_v2(db, sql, -1, out, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = "SequenceIndex: prepare failed: ";
        msg += sqlite3_errmsg(db);
        msg += " (sql: ";
        msg += sql;
        msg += ")";
        throw std::runtime_error(msg);
    }
}

} // namespace

SequenceIndex::SequenceIndex(const std::filesystem::path& db_path, unsigned max_depth)
    : db_path_(db_path), max_depth_(max_depth) {
    std::filesystem::create_directories(db_path_);
    open_db();
    ensure_schema();
    prepare_statements();
}

SequenceIndex::~SequenceIndex() {
    if (txn_active_ && db_) {
        char* err = nullptr;
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        txn_active_ = false;
    }
    finalize_statements();
    close_db();
}

void SequenceIndex::open_db() {
    auto path = db_path_ / "sequence_trie.sqlite";
    int rc = sqlite3_open(path.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = "SequenceIndex: cannot open " + path.string() + ": "
                          + (db_ ? sqlite3_errmsg(db_) : "unknown");
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(msg);
    }

    exec_or_throw(db_, "PRAGMA journal_mode=WAL",     "PRAGMA journal_mode");
    exec_or_throw(db_, "PRAGMA synchronous=NORMAL",   "PRAGMA synchronous");
    exec_or_throw(db_, "PRAGMA temp_store=MEMORY",    "PRAGMA temp_store");
}

void SequenceIndex::close_db() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void SequenceIndex::ensure_schema() {
    static const char* schema =
        "CREATE TABLE IF NOT EXISTS nodes("
        "  id INTEGER PRIMARY KEY"
        ");"
        "INSERT OR IGNORE INTO nodes(id) VALUES (0);"
        "CREATE TABLE IF NOT EXISTS edges("
        "  parent_id INTEGER NOT NULL,"
        "  move      INTEGER NOT NULL,"
        "  child_id  INTEGER NOT NULL,"
        "  PRIMARY KEY (parent_id, move)"
        ") WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS games("
        "  node_id INTEGER NOT NULL,"
        "  game_id INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS games_by_node ON games(node_id);";
    exec_or_throw(db_, schema, "schema");
}

void SequenceIndex::prepare_statements() {
    prepare_or_throw(db_,
        "SELECT child_id FROM edges WHERE parent_id = ? AND move = ?",
        &stmt_find_child_);
    prepare_or_throw(db_,
        "INSERT INTO nodes(id) VALUES (NULL)",
        &stmt_insert_node_);
    prepare_or_throw(db_,
        "INSERT INTO edges(parent_id, move, child_id) VALUES (?, ?, ?)",
        &stmt_insert_edge_);
    prepare_or_throw(db_,
        "INSERT INTO games(node_id, game_id) VALUES (?, ?)",
        &stmt_insert_game_);
    prepare_or_throw(db_,
        "SELECT COUNT(*) FROM games WHERE node_id = ?",
        &stmt_count_games_);
    prepare_or_throw(db_,
        "SELECT game_id FROM games WHERE node_id = ? ORDER BY rowid",
        &stmt_select_games_);
}

void SequenceIndex::finalize_statements() {
    sqlite3_finalize(stmt_find_child_);     stmt_find_child_     = nullptr;
    sqlite3_finalize(stmt_insert_node_);    stmt_insert_node_    = nullptr;
    sqlite3_finalize(stmt_insert_edge_);    stmt_insert_edge_    = nullptr;
    sqlite3_finalize(stmt_insert_game_);    stmt_insert_game_    = nullptr;
    sqlite3_finalize(stmt_count_games_);    stmt_count_games_    = nullptr;
    sqlite3_finalize(stmt_select_games_);   stmt_select_games_   = nullptr;
}

void SequenceIndex::begin_txn() const {
    if (txn_active_) return;
    exec_or_throw(db_, "BEGIN IMMEDIATE", "BEGIN");
    txn_active_ = true;
}

void SequenceIndex::commit_txn() const {
    if (!txn_active_) return;
    exec_or_throw(db_, "COMMIT", "COMMIT");
    txn_active_ = false;
}

SequenceIndex::NodeId SequenceIndex::find_child(NodeId parent, CompactMove move) const {
    sqlite3_reset(stmt_find_child_);
    sqlite3_clear_bindings(stmt_find_child_);
    sqlite3_bind_int64(stmt_find_child_, 1, parent);
    sqlite3_bind_int(stmt_find_child_, 2, static_cast<int>(move));

    int rc = sqlite3_step(stmt_find_child_);
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int64(stmt_find_child_, 0);
    }
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("SequenceIndex: find_child step: ")
                                 + sqlite3_errmsg(db_));
    }
    return -1;
}

SequenceIndex::NodeId SequenceIndex::create_child(NodeId parent, CompactMove move) {
    sqlite3_reset(stmt_insert_node_);
    if (sqlite3_step(stmt_insert_node_) != SQLITE_DONE) {
        throw std::runtime_error(std::string("SequenceIndex: insert_node: ")
                                 + sqlite3_errmsg(db_));
    }
    NodeId child = sqlite3_last_insert_rowid(db_);

    sqlite3_reset(stmt_insert_edge_);
    sqlite3_clear_bindings(stmt_insert_edge_);
    sqlite3_bind_int64(stmt_insert_edge_, 1, parent);
    sqlite3_bind_int(stmt_insert_edge_, 2, static_cast<int>(move));
    sqlite3_bind_int64(stmt_insert_edge_, 3, child);
    if (sqlite3_step(stmt_insert_edge_) != SQLITE_DONE) {
        throw std::runtime_error(std::string("SequenceIndex: insert_edge: ")
                                 + sqlite3_errmsg(db_));
    }
    return child;
}

SequenceIndex::NodeId SequenceIndex::walk(const std::vector<CompactMove>& moves) const {
    NodeId node = kRootId;
    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);
    for (unsigned i = 0; i < depth; ++i) {
        NodeId child = find_child(node, moves[i]);
        if (child < 0) return -1;
        node = child;
    }
    return node;
}

SequenceIndex::NodeId SequenceIndex::walk_or_create(const std::vector<CompactMove>& moves) {
    NodeId node = kRootId;
    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);
    for (unsigned i = 0; i < depth; ++i) {
        NodeId child = find_child(node, moves[i]);
        if (child < 0) child = create_child(node, moves[i]);
        node = child;
    }
    return node;
}

void SequenceIndex::insert(GameId game_id, const std::vector<CompactMove>& moves) {
    std::lock_guard lock(mutex_);
    begin_txn();

    auto depth = std::min(static_cast<unsigned>(moves.size()), max_depth_);

    auto record_game = [&](NodeId node) {
        sqlite3_reset(stmt_insert_game_);
        sqlite3_clear_bindings(stmt_insert_game_);
        sqlite3_bind_int64(stmt_insert_game_, 1, node);
        sqlite3_bind_int64(stmt_insert_game_, 2, static_cast<sqlite3_int64>(game_id));
        if (sqlite3_step(stmt_insert_game_) != SQLITE_DONE) {
            throw std::runtime_error(std::string("SequenceIndex: insert_game: ")
                                     + sqlite3_errmsg(db_));
        }
    };

    record_game(kRootId);

    NodeId node = kRootId;
    for (unsigned i = 0; i < depth; ++i) {
        NodeId child = find_child(node, moves[i]);
        if (child < 0) child = create_child(node, moves[i]);
        node = child;
        record_game(node);
    }
}

std::vector<GameId>
SequenceIndex::search_prefix(const std::vector<CompactMove>& moves) const {
    std::lock_guard lock(mutex_);
    NodeId node = walk(moves);
    if (node < 0) return {};

    std::vector<GameId> out;
    sqlite3_reset(stmt_select_games_);
    sqlite3_clear_bindings(stmt_select_games_);
    sqlite3_bind_int64(stmt_select_games_, 1, node);

    while (true) {
        int rc = sqlite3_step(stmt_select_games_);
        if (rc == SQLITE_ROW) {
            out.push_back(static_cast<GameId>(sqlite3_column_int64(stmt_select_games_, 0)));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            throw std::runtime_error(std::string("SequenceIndex: select_games: ")
                                     + sqlite3_errmsg(db_));
        }
    }
    return out;
}

std::uint32_t SequenceIndex::count(const std::vector<CompactMove>& moves) const {
    std::lock_guard lock(mutex_);
    NodeId node = walk(moves);
    if (node < 0) return 0;

    sqlite3_reset(stmt_count_games_);
    sqlite3_clear_bindings(stmt_count_games_);
    sqlite3_bind_int64(stmt_count_games_, 1, node);

    int rc = sqlite3_step(stmt_count_games_);
    if (rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("SequenceIndex: count step: ")
                                 + sqlite3_errmsg(db_));
    }
    return static_cast<std::uint32_t>(sqlite3_column_int64(stmt_count_games_, 0));
}

void SequenceIndex::save() const {
    std::lock_guard lock(mutex_);
    commit_txn();
    char* err = nullptr;
    sqlite3_exec(db_, "PRAGMA wal_checkpoint(PASSIVE)", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

void SequenceIndex::load() {
    // No-op: the SQLite file is the source of truth and is opened in the
    // constructor. Kept for API compatibility with the legacy in-memory trie.
}

} // namespace tictac
