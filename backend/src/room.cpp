#include "room.hpp"
#include "websocket_session.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string normalize_username(
    std::string value) {

    for (char& c : value) {
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)));
    }

    return value;
}

} // namespace

Room::Room(
    std::string id,
    std::shared_ptr<Database> database,
    std::string owner_key)
    : id_(std::move(id)),
      owner_key_(std::move(owner_key)),
      database_(std::move(database)) {
}

const std::string& Room::id() const {
    return id_;
}

bool Room::add(
    const std::shared_ptr<WebSocketSession>& session,
    const std::string& username) {

    std::lock_guard<std::mutex> lock(mutex_);

    members_.erase(
        std::remove_if(
            members_.begin(),
            members_.end(),
            [](const RoomMember& member) {
                return member.session.expired();
            }),
        members_.end());

    const auto wanted =
        normalize_username(username);

    for (const auto& member : members_) {

        if (normalize_username(
                member.username) == wanted) {
            return false;
        }
    }

    members_.push_back({
        session,
        username
    });

    return true;
}

bool Room::rename_member(
    const std::shared_ptr<WebSocketSession>& session,
    const std::string& username) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto wanted = normalize_username(username);

    for (const auto& member : members_) {
        auto locked = member.session.lock();
        if (!locked) continue;
        if (locked != session &&
            normalize_username(member.username) == wanted) {
            return false;
        }
    }

    for (auto& member : members_) {
        auto locked = member.session.lock();
        if (locked == session) {
            member.username = username;
            return true;
        }
    }

    return false;
}

bool Room::is_owner(
    const std::shared_ptr<WebSocketSession>& session) const {

    if (!session) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& member : members_) {
        if (auto locked = member.session.lock()) {
            if (locked == session) {
                return locked->user_key() == owner_key_;
            }
        }
    }

    return false;
}

void Room::remove(
    const std::shared_ptr<WebSocketSession>& session) {

    std::lock_guard<std::mutex> lock(mutex_);

    members_.erase(
        std::remove_if(
            members_.begin(),
            members_.end(),
            [&session](const RoomMember& member) {

                auto locked =
                    member.session.lock();

                return !locked ||
                       locked == session;
            }),
        members_.end());
}

void Room::broadcast(
    const std::string& message,
    const WebSocketSession* except) {

    std::vector<
        std::shared_ptr<WebSocketSession>
    > targets;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        members_.erase(
            std::remove_if(
                members_.begin(),
                members_.end(),
                [](const RoomMember& member) {
                    return member.session.expired();
                }),
            members_.end());

        for (const auto& member : members_) {

            if (auto session =
                    member.session.lock()) {

                if (session.get() != except) {
                    targets.push_back(
                        std::move(session));
                }
            }
        }
    }

    for (const auto& target : targets) {
        target->send(message);
    }
}

std::size_t Room::size() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t count = 0;

    for (const auto& member : members_) {
        if (!member.session.expired())
            ++count;
    }

    return count;
}

std::vector<std::string>
Room::usernames() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;

    for (const auto& member : members_) {

        if (!member.session.expired()) {
            result.push_back(
                member.username);
        }
    }

    return result;
}

std::vector<std::shared_ptr<WebSocketSession>>
Room::members_snapshot() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<WebSocketSession>> result;

    for (const auto& member : members_) {
        if (auto session = member.session.lock()) {
            result.push_back(std::move(session));
        }
    }

    return result;
}

void Room::clear_members() {
    std::lock_guard<std::mutex> lock(mutex_);
    members_.clear();
}

std::shared_ptr<Workspace>
Room::workspace() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!workspace_) {
        workspace_ =
            std::make_shared<Workspace>(
                id_,
                database_);
    }

    return workspace_;
}

std::shared_ptr<Database>
Room::database() const {
    return database_;
}
