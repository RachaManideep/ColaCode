#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

struct PersistedNode {
    std::string id;
    std::string parent_id;
    std::string name;
    std::string kind;
    std::string content;
    std::size_t version = 0;
    std::string language;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<PersistedNode> load_nodes(
        const std::string& room_id);

    bool load_node(
        const std::string& room_id,
        const std::string& node_id,
        PersistedNode& result);

    void create_node(
        const std::string& room_id,
        const PersistedNode& node);

    void save_node(
        const std::string& room_id,
        const PersistedNode& node);

    void delete_node(
        const std::string& room_id,
        const std::string& node_id);

    void rename_node(
        const std::string& room_id,
        const std::string& node_id,
        const std::string& name);

    void move_node(
        const std::string& room_id,
        const std::string& node_id,
        const std::string& parent_id);

    std::string room_owner(
        const std::string& room_id);

    void ensure_room(
        const std::string& room_id,
        const std::string& owner_key);

    void delete_room(
        const std::string& room_id);

private:
    void exec(const std::string& sql);
    void ensure_schema();
    void migrate_legacy_files();

    struct sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
