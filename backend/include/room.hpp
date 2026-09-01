#pragma once

#include "database.hpp"
#include "workspace.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class WebSocketSession;

struct RoomMember {
    std::weak_ptr<WebSocketSession> session;
    std::string username;
};

class Room {
public:
    Room(
        std::string id,
        std::shared_ptr<Database> database,
        std::string owner_key);

    const std::string& id() const;

    bool add(
        const std::shared_ptr<WebSocketSession>& session,
        const std::string& username);

    bool rename_member(
        const std::shared_ptr<WebSocketSession>& session,
        const std::string& username);

    bool is_owner(
        const std::shared_ptr<WebSocketSession>& session) const;

    void remove(
        const std::shared_ptr<WebSocketSession>& session);

    void broadcast(
        const std::string& message,
        const WebSocketSession* except = nullptr);

    std::size_t size() const;

    std::vector<std::string> usernames() const;

    std::vector<std::shared_ptr<WebSocketSession>> members_snapshot() const;

    void clear_members();

    std::shared_ptr<Workspace> workspace();

    std::shared_ptr<Database> database() const;

private:
    std::string id_;
    std::string owner_key_;

    mutable std::mutex mutex_;

    std::vector<RoomMember> members_;

    std::shared_ptr<Workspace> workspace_;
    std::shared_ptr<Database> database_;
};
