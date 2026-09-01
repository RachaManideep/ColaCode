#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

class RoomManager;
class Room;

class WebSocketSession
    : public std::enable_shared_from_this<WebSocketSession> {

public:
    WebSocketSession(
        boost::asio::ip::tcp::socket socket,
        std::shared_ptr<RoomManager> room_manager);

    void run();

    void send(
        const std::string& message);

    void clear_room_after_deletion(
        const std::string& room_id);

    const std::string& client_id() const;
    const std::string& user_key() const;

private:
    void on_accept(
        boost::beast::error_code ec);

    void do_read();

    void on_read(
        boost::beast::error_code ec,
        std::size_t bytes_transferred);

    void handle_message(
        const std::string& message);

    void join_room(
        const std::string& room_id,
        const std::string& username,
        const std::string& user_key);

    void leave_room();

    void handle_change_username(
        const std::string& message);

    void handle_edit(
        const std::string& message);

    void handle_create_node(
        const std::string& message);

    void handle_delete_node(
        const std::string& message);

    void handle_rename_node(
        const std::string& message);

    void handle_move_node(
        const std::string& message);

    void handle_open_file(
        const std::string& message);

    void handle_download_room();

    void handle_delete_room();

    void send_workspace(
        const std::shared_ptr<Room>& room);

    void broadcast_workspace(
        const std::shared_ptr<Room>& room);

    void send_document_state(
        const std::shared_ptr<Room>& room,
        const std::string& file_id);

    void send_presence(
        const std::shared_ptr<Room>& room);

    void broadcast_presence(
        const std::shared_ptr<Room>& room);

    void write_next();

    void on_write(
        boost::beast::error_code ec,
        std::size_t bytes_transferred);

    void fail(
        boost::beast::error_code ec,
        const char* what);

    boost::beast::websocket::stream<
        boost::asio::ip::tcp::socket
    > ws_;

    boost::beast::flat_buffer buffer_;

    std::shared_ptr<RoomManager> room_manager_;
    std::shared_ptr<Room> room_;

    std::string client_id_;
    std::string username_;
    std::string user_key_;

    mutable std::mutex state_mutex_;

    std::mutex write_mutex_;
    std::deque<std::string> write_queue_;
    bool writing_ = false;
};
