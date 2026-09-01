#include "http_server.hpp"
#include "room_manager.hpp"
#include "websocket_session.hpp"

#include <iostream>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

HttpServer::HttpServer(
    asio::io_context& ioc,
    const tcp::endpoint& endpoint,
    std::shared_ptr<RoomManager> room_manager)
    : ioc_(ioc),
      acceptor_(ioc),
      room_manager_(std::move(room_manager)) {

    boost::system::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) throw boost::system::system_error(ec);

    acceptor_.set_option(
        asio::socket_base::reuse_address(true),
        ec);
    if (ec) throw boost::system::system_error(ec);

    acceptor_.bind(endpoint, ec);
    if (ec) throw boost::system::system_error(ec);

    acceptor_.listen(
        asio::socket_base::max_listen_connections,
        ec);
    if (ec) throw boost::system::system_error(ec);
}

void HttpServer::run() {
    do_accept();
}

void HttpServer::do_accept() {

    acceptor_.async_accept(
        [this](
            boost::system::error_code ec,
            tcp::socket socket) {

            if (!ec) {

                std::make_shared<WebSocketSession>(
                    std::move(socket),
                    room_manager_)->run();

            } else {

                on_accept(ec);
            }

            do_accept();
        });
}

void HttpServer::on_accept(
    boost::system::error_code ec) {

    std::cerr
        << "[Server] accept: "
        << ec.message()
        << '\n';
}
