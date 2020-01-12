#include "TCPServer.h"

#include <exceptions.h>

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>

TCPServer::TCPServer() : Server(),
						 selector{} {
	selector.setReadCallback([this](auto fd, const auto data, auto buffer){onRead(fd, data, buffer);});
}

/**********************************************************************************************
 * bindSvr - Creates a network socket and sets it nonblocking so we can loop through looking for
 *           data. Then binds it to the ip address and port
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void TCPServer::bindSvr(const char *ip_addr, short unsigned int port) {
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (fd < 0)
		throw socket_error(std::string("failed to open server socket: ") + strerror(errno));
	
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	
	// bind
	{
		auto bindAddr = sockaddr_in{};
		bzero(&bindAddr, sizeof(bindAddr));
		if (inet_pton(AF_INET, ip_addr, &bindAddr.sin_addr) <= 0) // returns 0 on failure as well
			throw socket_error(std::string("failed to process IP address: ") + strerror(errno));
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_port = htons(port);
		if (bind(fd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0)
			throw socket_error(std::string("failed to bind server socket: ") + strerror(errno));
	}
	
	// listen
	if (listen(fd, 32) < 0)
		throw socket_error(std::string("failed to listen on server socket: ") + strerror(errno));
	
	selector.addFD(FD<void>(fd, nullptr, [this](int fd) -> std::shared_ptr<Buffer>{
		auto bindAddr = sockaddr_in{};
		auto bindAddrLength = socklen_t{};
		auto accepted = accept(fd, reinterpret_cast<sockaddr*>(&bindAddr), &bindAddrLength);
		if (accepted >= 0) {
			char addr[256];
			bzero(addr, 256);
			inet_ntop(AF_INET, &bindAddr, addr, bindAddrLength);
			fprintf(stdout, "Received connection %d from %s\n", accepted, addr);
			selector.addFD(accepted);
		}
		return nullptr;
	}, /* writeHandler */ [](auto, auto, auto){return -1;}, /* closeHandler */ [](auto fd){close(fd);}));
}

/**********************************************************************************************
 * listenSvr - Performs a loop to look for connections and create TCPConn objects to handle
 *             them. Also loops through the list of connections and handles data received and
 *             sending of data. 
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void TCPServer::listenSvr() {
	selector.selectLoop();
}

/**********************************************************************************************
 * shutdown - Cleanly closes the socket FD.
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void TCPServer::shutdown() {
	selector.clearFDs();
}

void TCPServer::onRead(int fd, const std::shared_ptr<void> &ref, DynamicBuffer buffer) {
	auto [length, data] = buffer.getNextChunk();
	fprintf(stdout, "Length: %ld\n", length);
	for (decltype(length) i = 0; i < length; i++) {
		fprintf(stdout, "%c", data[i]);
	}
	fprintf(stdout, "\n");
}
