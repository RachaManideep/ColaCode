#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>

class RoomManager;

class HttpServer {
public:
    HttpServer(
        boost::asio::io_context& ioc,
        const boost::asio::ip::tcp::endpoint& endpoint,
        std::shared_ptr<RoomManager> room_manager);

    void run();

private:
    void do_accept();

    void on_accept(
        boost::system::error_code ec);

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<RoomManager> room_manager_;
};
