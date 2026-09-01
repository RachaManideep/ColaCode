#include "database.hpp"
#include "http_server.hpp"
#include "room_manager.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/signal_set.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main() {

    try {

        constexpr unsigned short port = 8080;

        boost::asio::io_context ioc;

        auto database =
            std::make_shared<Database>(
                "collab.db");

        auto room_manager =
            std::make_shared<RoomManager>(
                database);

        HttpServer server(
            ioc,
            {
                boost::asio::ip::make_address(
                    "0.0.0.0"),
                port
            },
            room_manager);

        boost::asio::signal_set signals(
            ioc,
            SIGINT,
            SIGTERM);

        signals.async_wait(
            [&ioc](
                const boost::system::error_code&,
                int) {
                std::cout
                    << "\nShutting down...\n";
                ioc.stop();
            });

        server.run();

        const unsigned int thread_count =
            std::max(
                1u,
                std::thread::hardware_concurrency());

        std::vector<std::thread> threads;

        for (unsigned int i = 0;
             i < thread_count;
             ++i) {

            threads.emplace_back(
                [&ioc] {
                    ioc.run();
                });
        }

        std::cout
            << "Collaborative editor Phase V server\n"
            << "WebSocket: ws://localhost:"
            << port << '\n'
            << "SQLite: collab.db\n"
            << "Threads: "
            << thread_count
            << '\n';

        for (auto& thread : threads)
            thread.join();

    } catch (const std::exception& ex) {

        std::cerr
            << "Fatal error: "
            << ex.what()
            << '\n';

        return 1;
    }

    return 0;
}
