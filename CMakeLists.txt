cmake_minimum_required(VERSION 3.10.2)
project(AFIT-CSCE689-HW1-S)

set(CMAKE_CXX_STANDARD 17)

add_executable(Client src/client_main.cpp src/strfuncts.cpp include/strfuncts.h
               src/Client.cpp include/Client.h
               src/TCPClient.cpp include/TCPClient.h
               src/Selector.cpp include/Selector.h
               src/NetworkMessage.cpp include/NetworkMessage.h
               include/config.h include/exceptions.h include/strfuncts.h)
add_executable(Server src/server_main.cpp src/strfuncts.cpp include/strfuncts.h
               src/Server.cpp include/Server.h
               src/TCPServer.cpp include/TCPServer.h
               src/Selector.cpp include/Selector.h
               src/NetworkMessage.cpp include/NetworkMessage.h
               include/config.h include/exceptions.h include/strfuncts.h)

target_include_directories(Client PRIVATE src include)
target_include_directories(Server PRIVATE src include)
