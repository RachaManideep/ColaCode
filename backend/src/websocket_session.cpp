#include "websocket_session.hpp"
#include "room.hpp"
#include "room_manager.hpp"
#include "workspace.hpp"
#include "room_archive.hpp"

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <cctype>
#include <iostream>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;

namespace {

std::atomic<unsigned long long> next_client_id{1};
std::atomic<unsigned long long> next_room_node_id{1};

std::string make_client_id() {
    return "user-" +
           std::to_string(
               next_client_id.fetch_add(1));
}

std::string json_escape(
    const std::string& input) {

    std::string out;

    for (char c : input) {

        switch (c) {

            case '"':
                out += "\\\"";
                break;

            case '\\':
                out += "\\\\";
                break;

            case '\n':
                out += "\\n";
                break;

            case '\r':
                out += "\\r";
                break;

            case '\t':
                out += "\\t";
                break;

            default:
                out += c;
                break;
        }
    }

    return out;
}

std::string get_json_string(
    const std::string& json,
    const std::string& key) {

    const std::string needle =
        "\"" + key + "\"";

    const auto key_pos =
        json.find(needle);

    if (key_pos == std::string::npos)
        return {};

    const auto colon =
        json.find(
            ':',
            key_pos + needle.size());

    if (colon == std::string::npos)
        return {};

    const auto quote =
        json.find('"', colon + 1);

    if (quote == std::string::npos)
        return {};

    std::string value;
    bool escaped = false;

    for (std::size_t i = quote + 1;
         i < json.size();
         ++i) {

        const char c = json[i];

        if (escaped) {

            switch (c) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                default: value += c; break;
            }

            escaped = false;

        } else if (c == '\\') {

            escaped = true;

        } else if (c == '"') {

            return value;

        } else {

            value += c;
        }
    }

    return {};
}

std::size_t get_json_number(
    const std::string& json,
    const std::string& key,
    std::size_t fallback = 0) {

    const std::string needle =
        "\"" + key + "\"";

    const auto key_pos =
        json.find(needle);

    if (key_pos == std::string::npos)
        return fallback;

    const auto colon =
        json.find(
            ':',
            key_pos + needle.size());

    if (colon == std::string::npos)
        return fallback;

    const auto start =
        json.find_first_of(
            "0123456789",
            colon + 1);

    if (start == std::string::npos)
        return fallback;

    try {
        return std::stoull(
            json.substr(start));
    } catch (...) {
        return fallback;
    }
}

bool valid_room_id(
    const std::string& value) {

    if (value.empty() ||
        value.size() > 64)
        return false;

    for (char c : value) {

        if (!(std::isalnum(
                  static_cast<unsigned char>(c)) ||
              c == '-' ||
              c == '_')) {
            return false;
        }
    }

    return true;
}

bool valid_username(
    const std::string& value) {

    if (value.empty() ||
        value.size() > 32)
        return false;

    for (char c : value) {

        if (!(std::isalnum(
                  static_cast<unsigned char>(c)) ||
              c == '_' ||
              c == '-')) {
            return false;
        }
    }

    return true;
}

} // namespace

WebSocketSession::WebSocketSession(
    asio::ip::tcp::socket socket,
    std::shared_ptr<RoomManager> room_manager)
    : ws_(std::move(socket)),
      room_manager_(std::move(room_manager)),
      client_id_(make_client_id()) {
}

void WebSocketSession::run() {

    ws_.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::server));

    ws_.set_option(
        websocket::stream_base::decorator(
            [](websocket::response_type& response) {
                response.set(
                    boost::beast::http::field::server,
                    "collab-editor-phase5");
            }));

    ws_.async_accept(
        beast::bind_front_handler(
            &WebSocketSession::on_accept,
            shared_from_this()));
}

void WebSocketSession::on_accept(
    beast::error_code ec) {

    if (ec) {
        fail(ec, "accept");
        return;
    }

    send(
        "{\"type\":\"welcome\","
        "\"client_id\":\"" +
        json_escape(client_id_) +
        "\"}");

    do_read();
}

void WebSocketSession::do_read() {

    buffer_.clear();

    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketSession::on_read,
            shared_from_this()));
}

void WebSocketSession::on_read(
    beast::error_code ec,
    std::size_t) {

    if (ec == websocket::error::closed) {
        leave_room();
        return;
    }

    if (ec) {
        leave_room();
        fail(ec, "read");
        return;
    }

    handle_message(
        beast::buffers_to_string(
            buffer_.data()));

    do_read();
}

void WebSocketSession::handle_message(
    const std::string& message) {

    const auto type =
        get_json_string(message, "type");

    if (type == "join_room") {

        const auto room_id =
            get_json_string(
                message,
                "room_id");

        const auto username =
            get_json_string(
                message,
                "username");

        const auto user_key =
            get_json_string(
                message,
                "user_key");

        if (!valid_room_id(room_id) ||
            !valid_username(username)) {

            send(
                "{\"type\":\"error\","
                "\"message\":\"invalid room or username\"}");

            return;
        }

        join_room(room_id, username, user_key);
        return;
    }

    if (type == "leave_room") {
        leave_room();
        return;
    }

    if (type == "change_username") {
        handle_change_username(message);
        return;
    }

    if (type == "edit") {
        handle_edit(message);
        return;
    }

    if (type == "create_file") {
        handle_create_node(message);
        return;
    }

    if (type == "create_folder") {
        handle_create_node(message);
        return;
    }

    if (type == "delete_node") {
        handle_delete_node(message);
        return;
    }

    if (type == "rename_node") {
        handle_rename_node(message);
        return;
    }

    if (type == "move_node") {
        handle_move_node(message);
        return;
    }

    if (type == "open_file") {
        handle_open_file(message);
        return;
    }

    if (type == "download_room") {
        handle_download_room();
        return;
    }

    if (type == "delete_room") {
        handle_delete_room();
        return;
    }

    send(
        "{\"type\":\"error\","
        "\"message\":\"unknown message type\"}");
}

void WebSocketSession::join_room(
    const std::string& room_id,
    const std::string& username,
    const std::string& user_key) {

    leave_room();

    const auto effective_user_key =
        user_key.empty() ? client_id_ : user_key;

    auto new_room =
        room_manager_->get_or_create(
            room_id,
            effective_user_key);

    if (!new_room->add(
            shared_from_this(),
            username)) {

        send(
            "{\"type\":\"join_rejected\","
            "\"reason\":\"username_taken\","
            "\"message\":\"That username is already in use in this room.\"}");

        return;
    }

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);

        room_ = new_room;
        username_ = username;
        user_key_ = effective_user_key;
    }

    send(
        "{\"type\":\"room_joined\","
        "\"room_id\":\"" +
        json_escape(room_id) +
        "\","
        "\"username\":\"" +
        json_escape(username_) +
        "\","
        "\"member_count\":" +
        std::to_string(new_room->size()) +
        ",\"owner\":" +
        (new_room->is_owner(shared_from_this()) ? "true" : "false") +
        "}");

    send_workspace(new_room);
    send_presence(new_room);

    // Existing members need the updated collaborator list too.
    broadcast_presence(new_room);

    new_room->broadcast(
        "{\"type\":\"user_joined\","
        "\"client_id\":\"" +
        json_escape(client_id_) +
        "\","
        "\"username\":\"" +
        json_escape(username_) +
        "\","
        "\"member_count\":" +
        std::to_string(new_room->size()) +
        "}",
        this);

    std::cout
        << "[Room] "
        << username_
        << " (" << client_id_ << ")"
        << " joined "
        << room_id
        << " ("
        << new_room->size()
        << " members)"
        << '\n';
}

void WebSocketSession::leave_room() {

    std::shared_ptr<Room> old_room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);

        old_room = room_;
        room_.reset();
    }

    if (!old_room)
        return;

    old_room->remove(
        shared_from_this());

    old_room->broadcast(
        "{\"type\":\"user_left\","
        "\"client_id\":\"" +
        json_escape(client_id_) +
        "\","
        "\"username\":\"" +
        json_escape(username_) +
        "\","
        "\"member_count\":" +
        std::to_string(old_room->size()) +
        "}");

    // Remaining members need the new collaborator list.
    broadcast_presence(old_room);

    room_manager_->remove_if_empty(
        old_room->id(),
        old_room);
}

void WebSocketSession::handle_change_username(
    const std::string& message) {

    const auto new_username =
        get_json_string(message, "username");

    if (!valid_username(new_username)) {
        send("{\"type\":\"username_rejected\",\"message\":\"Invalid username.\"}");
        return;
    }

    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        room = room_;
    }

    if (!room) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        username_ = new_username;
        send("{\"type\":\"username_changed\",\"username\":\"" +
             json_escape(username_) + "\"}");
        return;
    }

    if (!room->rename_member(shared_from_this(), new_username)) {
        send("{\"type\":\"username_rejected\",\"reason\":\"username_taken\",\"message\":\"That username is already in use in this room.\"}");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        username_ = new_username;
    }

    send("{\"type\":\"username_changed\",\"username\":\"" +
         json_escape(username_) + "\"}");
    send_presence(room);
    broadcast_presence(room);
    room->broadcast(
        "{\"type\":\"user_renamed\",\"client_id\":\"" +
        json_escape(client_id_) + "\",\"username\":\"" +
        json_escape(username_) + "\"}",
        this);
}

void WebSocketSession::handle_create_node(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    const auto parent_id =
        get_json_string(
            message,
            "parent_id");

    auto kind =
        get_json_string(
            message,
            "kind");

    if (kind.empty()) {
        const auto type =
            get_json_string(
                message,
                "type");

        kind =
            type == "create_folder"
                ? "folder"
                : "file";
    }

    const auto name =
        get_json_string(
            message,
            "name");

    if (name.empty()) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"name is required\"}");
        return;
    }

    std::string created_id;

    if (!room->workspace()->create(
            parent_id,
            name,
            kind,
            created_id)) {

        send(
            "{\"type\":\"error\","
            "\"message\":\"cannot create node: duplicate name, invalid parent, or invalid name\"}");
        return;
    }

    send_workspace(room);
    broadcast_workspace(room);

    room->broadcast(
        "{\"type\":\"workspace_changed\","
        "\"action\":\"create\","
        "\"node_id\":\"" +
        json_escape(created_id) +
        "\"}",
        this);
}

void WebSocketSession::handle_delete_node(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    const auto node_id =
        get_json_string(
            message,
            "node_id");

    if (!room->workspace()->remove(node_id)) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"cannot delete node\"}");
        return;
    }

    send_workspace(room);
    broadcast_workspace(room);

    room->broadcast(
        "{\"type\":\"workspace_changed\","
        "\"action\":\"delete\","
        "\"node_id\":\"" +
        json_escape(node_id) +
        "\"}",
        this);
}

void WebSocketSession::handle_rename_node(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    const auto node_id =
        get_json_string(
            message,
            "node_id");

    const auto name =
        get_json_string(
            message,
            "name");

    if (!room->workspace()->rename(
            node_id,
            name)) {

        send(
            "{\"type\":\"error\","
            "\"message\":\"cannot rename node: duplicate or invalid name\"}");
        return;
    }

    send_workspace(room);
    broadcast_workspace(room);

    room->broadcast(
        "{\"type\":\"workspace_changed\","
        "\"action\":\"rename\","
        "\"node_id\":\"" +
        json_escape(node_id) +
        "\"}",
        this);
}

void WebSocketSession::handle_move_node(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    const auto node_id =
        get_json_string(
            message,
            "node_id");

    const auto parent_id =
        get_json_string(
            message,
            "parent_id");

    if (!room->workspace()->move(
            node_id,
            parent_id)) {

        send(
            "{\"type\":\"error\","
            "\"message\":\"cannot move node\"}");
        return;
    }

    send_workspace(room);
    broadcast_workspace(room);

    room->broadcast(
        "{\"type\":\"workspace_changed\","
        "\"action\":\"move\","
        "\"node_id\":\"" +
        json_escape(node_id) +
        "\"}",
        this);
}

void WebSocketSession::handle_open_file(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    const auto file_id =
        get_json_string(
            message,
            "file_id");

    send_document_state(
        room,
        file_id);
}

void WebSocketSession::handle_edit(
    const std::string& message) {

    std::shared_ptr<Room> room;

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_);
        room = room_;
    }

    if (!room) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"join a room first\"}");
        return;
    }

    EditOperation operation;

    operation.file_id =
        get_json_string(
            message,
            "file_id");

    operation.base_version =
        get_json_number(
            message,
            "base_version");

    operation.position =
        get_json_number(
            message,
            "position");

    operation.delete_count =
        get_json_number(
            message,
            "delete_count");

    operation.insert_text =
        get_json_string(
            message,
            "insert_text");

    auto document =
        room->workspace()->get_document(
            operation.file_id);

    if (!document) {
        send(
            "{\"type\":\"error\","
            "\"message\":\"file not found\"}");
        return;
    }

    AppliedEdit result;

    if (!document->apply(
            operation,
            result)) {

        send(
            "{\"type\":\"edit_rejected\","
            "\"file_id\":\"" +
            json_escape(operation.file_id) +
            "\","
            "\"version\":" +
            std::to_string(document->version()) +
            ","
            "\"content\":\"" +
            json_escape(document->content()) +
            "\"}");

        return;
    }

    PersistedNode persisted;

    persisted.id =
        result.operation.file_id;

    persisted.content =
        result.content;

    persisted.version =
        result.version;

    room->database()->save_node(
        room->id(),
        persisted);

    // Broadcast the authoritative document after every accepted edit.
    // This keeps all clients synchronized even when several local edits
    // are queued before their version acknowledgements arrive.
    const auto content = document->content();

    room->broadcast(
        "{\"type\":\"document_state\","
        "\"file_id\":\"" +
        json_escape(result.operation.file_id) +
        "\","
        "\"version\":" +
        std::to_string(result.version) +
        ","
        "\"content\":\"" +
        json_escape(content) +
        "\"}");
}

void WebSocketSession::handle_download_room() {

    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        room = room_;
    }

    if (!room) {
        send("{\"type\":\"error\",\"message\":\"join a room first\"}");
        return;
    }

    try {
        const auto archive =
            make_room_zip_base64(*room->workspace());

        send(
            "{\"type\":\"room_archive\",\"room_id\":\"" +
            json_escape(room->id()) +
            "\",\"archive_base64\":\"" +
            archive + "\"}");
    } catch (const std::exception& ex) {
        send(
            "{\"type\":\"error\",\"message\":\"download failed: " +
            json_escape(ex.what()) + "\"}");
    }
}

void WebSocketSession::handle_delete_room() {

    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        room = room_;
    }

    if (!room) {
        send("{\"type\":\"error\",\"message\":\"join a room first\"}");
        return;
    }

    const auto room_id = room->id();

    if (!room->is_owner(shared_from_this())) {
        send(
            "{\"type\":\"error\",\"message\":\"Only the room owner can delete this room.\"}");
        return;
    }

    const auto members = room->members_snapshot();

    if (!room_manager_->delete_room(room_id, room)) {
        send("{\"type\":\"error\",\"message\":\"room could not be deleted\"}");
        return;
    }

    room->clear_members();

    const std::string message =
        "{\"type\":\"room_deleted\",\"room_id\":\"" +
        json_escape(room_id) + "\"}";

    for (const auto& member : members) {
        member->send(message);
        member->clear_room_after_deletion(room_id);
    }

    std::cout << "[Room] deleted " << room_id << '\n';
}

void WebSocketSession::send_workspace(
    const std::shared_ptr<Room>& room) {

    const auto nodes =
        room->workspace()->nodes();

    std::string message =
        "{\"type\":\"workspace_state\",\"nodes\":[";

    for (std::size_t i = 0;
         i < nodes.size();
         ++i) {

        if (i != 0)
            message += ',';

        message +=
            "{\"id\":\"" +
            json_escape(nodes[i].id) +
            "\","
            "\"parent_id\":\"" +
            json_escape(nodes[i].parent_id) +
            "\","
            "\"name\":\"" +
            json_escape(nodes[i].name) +
            "\","
            "\"kind\":\"" +
            json_escape(nodes[i].kind) +
            "\","
            "\"language\":\"" +
            json_escape(nodes[i].language) +
            "\"}";
    }

    message += "]}";

    send(message);
}

void WebSocketSession::broadcast_workspace(
    const std::shared_ptr<Room>& room) {

    const auto nodes =
        room->workspace()->nodes();

    std::string message =
        "{\"type\":\"workspace_state\",\"nodes\":[";

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i != 0) message += ',';

        message +=
            "{\"id\":\"" + json_escape(nodes[i].id) +
            "\",\"parent_id\":\"" + json_escape(nodes[i].parent_id) +
            "\",\"name\":\"" + json_escape(nodes[i].name) +
            "\",\"kind\":\"" + json_escape(nodes[i].kind) +
            "\",\"language\":\"" + json_escape(nodes[i].language) + "\"}";
    }

    message += "]}";
    room->broadcast(message, this);
}

void WebSocketSession::send_presence(
    const std::shared_ptr<Room>& room) {

    const auto users =
        room->usernames();

    std::string message =
        "{\"type\":\"presence\",\"users\":[";

    for (std::size_t i = 0;
         i < users.size();
         ++i) {

        if (i != 0)
            message += ',';

        message +=
            "\"" +
            json_escape(users[i]) +
            "\"";
    }

    message += "]}";

    send(message);
}

void WebSocketSession::broadcast_presence(
    const std::shared_ptr<Room>& room) {

    const auto users = room->usernames();
    std::string message =
        "{\"type\":\"presence\",\"users\":[";

    for (std::size_t i = 0; i < users.size(); ++i) {
        if (i != 0) message += ',';
        message += "\"" + json_escape(users[i]) + "\"";
    }

    message += "]}";
    room->broadcast(message, this);
}

void WebSocketSession::send_document_state(
    const std::shared_ptr<Room>& room,
    const std::string& file_id) {

    auto document =
        room->workspace()->get_document(
            file_id);

    if (!document) {

        send(
            "{\"type\":\"error\","
            "\"message\":\"file not found\"}");

        return;
    }

    send(
        "{\"type\":\"document_state\","
        "\"file_id\":\"" +
        json_escape(file_id) +
        "\","
        "\"version\":" +
        std::to_string(document->version()) +
        ","
        "\"content\":\"" +
        json_escape(document->content()) +
        "\"}");
}

void WebSocketSession::clear_room_after_deletion(
    const std::string& room_id) {

    std::lock_guard<std::mutex> lock(state_mutex_);

    if (room_ && room_->id() == room_id) {
        room_.reset();
    }
}

const std::string& WebSocketSession::user_key() const {
    return user_key_;
}

void WebSocketSession::send(
    const std::string& message) {

    std::lock_guard<std::mutex> lock(
        write_mutex_);

    write_queue_.push_back(message);

    if (writing_)
        return;

    writing_ = true;
    write_next();
}

void WebSocketSession::write_next() {

    ws_.text(true);

    ws_.async_write(
        asio::buffer(
            write_queue_.front()),

        beast::bind_front_handler(
            &WebSocketSession::on_write,
            shared_from_this()));
}

void WebSocketSession::on_write(
    beast::error_code ec,
    std::size_t) {

    std::lock_guard<std::mutex> lock(
        write_mutex_);

    if (ec) {
        writing_ = false;
        fail(ec, "write");
        return;
    }

    write_queue_.pop_front();

    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    write_next();
}

const std::string&
WebSocketSession::client_id() const {
    return client_id_;
}

void WebSocketSession::fail(
    beast::error_code ec,
    const char* what) {

    if (ec ==
        asio::error::operation_aborted) {
        return;
    }

    std::cerr
        << "[WS] "
        << what
        << ": "
        << ec.message()
        << '\n';
}
