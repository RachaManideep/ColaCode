cmake_minimum_required(VERSION 3.16)
project(collab_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system)
find_package(SQLite3 REQUIRED)

add_executable(collab_server
    src/main.cpp
    src/http_server.cpp
    src/websocket_session.cpp
    src/room.cpp
    src/room_manager.cpp
    src/document.cpp
    src/database.cpp
    src/workspace.cpp
    src/room_archive.cpp
)

target_include_directories(collab_server PRIVATE include)
target_link_libraries(collab_server PRIVATE Boost::system SQLite::SQLite3)
