#include "room_manager.hpp"

RoomManager::RoomManager(
    std::shared_ptr<Database> database)
    : database_(std::move(database)) {
}

std::shared_ptr<Room>
RoomManager::get_or_create(
    const std::string& room_id,
    const std::string& owner_key) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);

    if (it != rooms_.end())
        return it->second;

    auto owner = database_->room_owner(room_id);
    if (owner.empty()) {
        database_->ensure_room(room_id, owner_key);
        owner = owner_key;
    }

    auto room =
        std::make_shared<Room>(
            room_id,
            database_,
            owner);

    rooms_[room_id] = room;

    return room;
}

void RoomManager::remove_if_empty(
    const std::string& room_id,
    const std::shared_ptr<Room>& room) {

    if (room->size() != 0)
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);

    if (it != rooms_.end() &&
        it->second == room) {
        rooms_.erase(it);
    }
}


bool RoomManager::delete_room(
    const std::string& room_id,
    const std::shared_ptr<Room>& room) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end() || it->second != room)
        return false;

    rooms_.erase(it);
    database_->delete_room(room_id);
    return true;
}
