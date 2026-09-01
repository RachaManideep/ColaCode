#pragma once

#include "room.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class RoomManager {
public:
    explicit RoomManager(
        std::shared_ptr<Database> database);

    std::shared_ptr<Room> get_or_create(
        const std::string& room_id,
        const std::string& owner_key);

    void remove_if_empty(
        const std::string& room_id,
        const std::shared_ptr<Room>& room);

    bool delete_room(
        const std::string& room_id,
        const std::shared_ptr<Room>& room);

private:
    std::mutex mutex_;

    std::unordered_map<
        std::string,
        std::shared_ptr<Room>
    > rooms_;

    std::shared_ptr<Database> database_;
};
