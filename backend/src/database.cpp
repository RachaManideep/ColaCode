#include "database.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace {

void check_sqlite(int rc, sqlite3* db, const char* operation) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        std::string message = operation;
        message += ": ";
        message += sqlite3_errmsg(db);
        throw std::runtime_error(message);
    }
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        check_sqlite(
            sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr),
            db_,
            "sqlite3_prepare_v2");
    }

    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    sqlite3_stmt* get() { return stmt_; }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

} // namespace

Database::Database(const std::string& path) {
    const int rc = sqlite3_open(path.c_str(), &db_);

    if (rc != SQLITE_OK) {
        std::string message = "sqlite3_open: ";
        message += sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(message);
    }

    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    ensure_schema();
    migrate_legacy_files();
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::exec(const std::string& sql) {
    char* error = nullptr;

    const int rc = sqlite3_exec(
        db_,
        sql.c_str(),
        nullptr,
        nullptr,
        &error);

    if (rc != SQLITE_OK) {
        std::string message =
            error ? error : "unknown sqlite error";

        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void Database::ensure_schema() {
    exec(R"SQL(
        -- Kept for migration from the previous Phase V schema.
        CREATE TABLE IF NOT EXISTS files (
            room_id TEXT NOT NULL,
            file_id TEXT NOT NULL,
            version INTEGER NOT NULL DEFAULT 0,
            content TEXT NOT NULL DEFAULT '',
            updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (room_id, file_id)
        );

        CREATE TABLE IF NOT EXISTS rooms (
            room_id TEXT PRIMARY KEY,
            owner_key TEXT NOT NULL,
            created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY,
            room_id TEXT NOT NULL,
            parent_id TEXT NOT NULL DEFAULT '',
            name TEXT NOT NULL,
            kind TEXT NOT NULL CHECK(kind IN ('file', 'folder')),
            content TEXT NOT NULL DEFAULT '',
            version INTEGER NOT NULL DEFAULT 0,
            language TEXT NOT NULL DEFAULT '',
            created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(room_id, parent_id, name COLLATE NOCASE)
        );

        CREATE INDEX IF NOT EXISTS idx_nodes_room
        ON nodes(room_id);

        CREATE INDEX IF NOT EXISTS idx_nodes_parent
        ON nodes(room_id, parent_id);
    )SQL");
}

void Database::migrate_legacy_files() {
    // The old Phase V used a "files" table with:
    // room_id, file_id, version, content.
    // If that table exists, copy rows that aren't already represented.
    exec(R"SQL(
        INSERT OR IGNORE INTO nodes
            (id, room_id, parent_id, name, kind, content, version, language)
        SELECT
            'legacy-' || room_id || '-' || file_id,
            room_id,
            '',
            file_id,
            'file',
            content,
            version,
            CASE
                WHEN lower(file_id) LIKE '%.cpp' THEN 'cpp'
                WHEN lower(file_id) LIKE '%.h' THEN 'cpp'
                WHEN lower(file_id) LIKE '%.hpp' THEN 'cpp'
                WHEN lower(file_id) LIKE '%.js' THEN 'javascript'
                WHEN lower(file_id) LIKE '%.ts' THEN 'typescript'
                WHEN lower(file_id) LIKE '%.tsx' THEN 'typescript'
                WHEN lower(file_id) LIKE '%.py' THEN 'python'
                WHEN lower(file_id) LIKE '%.java' THEN 'java'
                WHEN lower(file_id) LIKE '%.json' THEN 'json'
                WHEN lower(file_id) LIKE '%.md' THEN 'markdown'
                ELSE ''
            END
        FROM files;
    )SQL");
}

std::vector<PersistedNode>
Database::load_nodes(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "SELECT id, parent_id, name, kind, content, version, language "
        "FROM nodes WHERE room_id = ? "
        "ORDER BY parent_id, kind DESC, name;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    std::vector<PersistedNode> result;

    while (true) {
        const int rc = sqlite3_step(statement.get());

        if (rc == SQLITE_DONE) break;

        check_sqlite(rc, db_, "select nodes");

        PersistedNode node;

        const auto text = [&](int index) -> std::string {
            const unsigned char* value =
                sqlite3_column_text(statement.get(), index);

            return value
                ? reinterpret_cast<const char*>(value)
                : "";
        };

        node.id = text(0);
        node.parent_id = text(1);
        node.name = text(2);
        node.kind = text(3);
        node.content = text(4);
        node.version =
            static_cast<std::size_t>(
                sqlite3_column_int64(statement.get(), 5));
        node.language = text(6);

        result.push_back(std::move(node));
    }

    return result;
}

bool Database::load_node(
    const std::string& room_id,
    const std::string& node_id,
    PersistedNode& result) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "SELECT id, parent_id, name, kind, content, version, language "
        "FROM nodes WHERE room_id = ? AND id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, node_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind node_id");

    const int rc = sqlite3_step(statement.get());

    if (rc == SQLITE_DONE) return false;

    check_sqlite(rc, db_, "select node");

    const auto text = [&](int index) -> std::string {
        const unsigned char* value =
            sqlite3_column_text(statement.get(), index);

        return value
            ? reinterpret_cast<const char*>(value)
            : "";
    };

    result.id = text(0);
    result.parent_id = text(1);
    result.name = text(2);
    result.kind = text(3);
    result.content = text(4);
    result.version =
        static_cast<std::size_t>(
            sqlite3_column_int64(statement.get(), 5));
    result.language = text(6);

    return true;
}

void Database::create_node(
    const std::string& room_id,
    const PersistedNode& node) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "INSERT INTO nodes "
        "(id, room_id, parent_id, name, kind, content, version, language) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, node.id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 3, node.parent_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind parent_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 4, node.name.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind name");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 5, node.kind.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind kind");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 6, node.content.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind content");

    check_sqlite(
        sqlite3_bind_int64(
            statement.get(), 7, static_cast<sqlite3_int64>(node.version)),
        db_, "bind version");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 8, node.language.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind language");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "insert node");
}

void Database::save_node(
    const std::string& room_id,
    const PersistedNode& node) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "UPDATE nodes SET "
        "content = ?, version = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE room_id = ? AND id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, node.content.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind content");

    check_sqlite(
        sqlite3_bind_int64(
            statement.get(), 2, static_cast<sqlite3_int64>(node.version)),
        db_, "bind version");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 3, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 4, node.id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind id");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "save node");
}

void Database::delete_node(
    const std::string& room_id,
    const std::string& node_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "DELETE FROM nodes "
        "WHERE room_id = ? AND id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, node_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind node_id");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "delete node");
}

void Database::rename_node(
    const std::string& room_id,
    const std::string& node_id,
    const std::string& name) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "UPDATE nodes SET "
        "name = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE room_id = ? AND id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind name");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 3, node_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind node_id");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "rename node");
}

void Database::move_node(
    const std::string& room_id,
    const std::string& node_id,
    const std::string& parent_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "UPDATE nodes SET "
        "parent_id = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE room_id = ? AND id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, parent_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind parent_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 3, node_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind node_id");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "move node");
}

std::string Database::room_owner(
    const std::string& room_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "SELECT owner_key FROM rooms WHERE room_id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    const int rc = sqlite3_step(statement.get());
    if (rc == SQLITE_ROW) {
        const auto* value = sqlite3_column_text(statement.get(), 0);
        return value ? reinterpret_cast<const char*>(value) : std::string{};
    }

    if (rc != SQLITE_DONE) {
        check_sqlite(rc, db_, "load room owner");
    }

    return {};
}

void Database::ensure_room(
    const std::string& room_id,
    const std::string& owner_key) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "INSERT OR IGNORE INTO rooms(room_id, owner_key) VALUES(?, ?);");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 2, owner_key.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind owner_key");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "ensure room");
}

void Database::delete_room(
    const std::string& room_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    Statement statement(
        db_,
        "DELETE FROM nodes WHERE room_id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            statement.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_step(statement.get()),
        db_, "delete room");

    // Also remove rows from the legacy table so a deleted room cannot
    // reappear through the migration path if the room is recreated.
    Statement legacy(
        db_,
        "DELETE FROM files WHERE room_id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            legacy.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind legacy room_id");

    check_sqlite(
        sqlite3_step(legacy.get()),
        db_, "delete legacy room");

    Statement room(
        db_,
        "DELETE FROM rooms WHERE room_id = ?;");

    check_sqlite(
        sqlite3_bind_text(
            room.get(), 1, room_id.c_str(), -1, SQLITE_TRANSIENT),
        db_, "bind room_id");

    check_sqlite(
        sqlite3_step(room.get()),
        db_, "delete room metadata");
}
