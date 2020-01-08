cmake_minimum_required(VERSION 3.10.2)
project(AFIT-CSCE689-HW1-S)

set(CMAKE_CXX_STANDARD 17)

add_executable(Client src/client_main.cpp src/Client.h src/Client.cpp src/TCPClient.cpp src/TCPClient.h)
add_executable(Server src/server_main.cpp src/Server.h src/Server.cpp src/TCPServer.cpp src/TCPServer.h)

target_include_directories(Client PRIVATE src)
target_include_directories(Server PRIVATE src)
