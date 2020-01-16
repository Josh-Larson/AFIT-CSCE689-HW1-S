#include "TCPServer.h"

#include <exceptions.h>

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>
#include <NetworkMessage.h>

TCPServer::TCPServer() : Server(),
						 selector{} {
	selector.setReadCallback([this](auto fd, const auto data, auto & buffer){onRead(fd, data, buffer);});
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
		auto accepted = accept4(fd, reinterpret_cast<sockaddr*>(&bindAddr), &bindAddrLength, SOCK_NONBLOCK);
		if (accepted >= 0) {
			std::array<char, 256> addr{};
			inet_ntop(AF_INET, &bindAddr, addr.data(), addr.size());
			fprintf(stdout, "Received connection %d from %s\n", accepted, addr.data());
			auto greeting = createGreeting();
			selector.addFD(accepted);
			selector.writeToFD(accepted, std::make_shared<Buffer>(greeting));
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
	auto code = selector.selectLoop();
	switch (code) {
		case SelectLoopTermination::SUCCESS:
		default:
			fprintf(stdout, "\nShutting down...\n");
			break;
		case SelectLoopTermination::INTERRUPTED:
			fprintf(stdout, "\nReceived signal requesting shutdown. Shutting down...\n");
			break;
		case SelectLoopTermination::SOCKET_ERROR:
			fprintf(stdout, "\nUnknown socket error: %s\n", strerror(errno));
			break;
	}
}

/**********************************************************************************************
 * shutdown - Cleanly closes the socket FD.
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void TCPServer::shutdown() {
	selector.clearFDs();
}

/* Not ideal.. but works for the time being */
#define HANDLE_MESSAGE(FUNCTION, MESSAGE_TYPE)  {\
                                                    MESSAGE_TYPE msg;\
                                                    if (buffer.getNext(&msg, sizeof(MESSAGE_TYPE))) \
                                                        FUNCTION(fd, data, msg); \
                                                }

void TCPServer::onRead(int fd, StoredDataPointer data, DynamicBuffer & buffer) {
	Message message{};
	if (buffer.peekNext(&message, sizeof(Message))) {
		fprintf(stdout, "Received message: %d\n", static_cast<int>(message.type));
		switch (message.type) {
			case MessageType::HELLO:     HANDLE_MESSAGE(onReadHelloRequest,    HelloMessage) break;
			case MessageType::GENERIC_1: HANDLE_MESSAGE(onReadGeneric1Request, Generic1Message) break;
			case MessageType::GENERIC_2: HANDLE_MESSAGE(onReadGeneric2Request, Generic2Message) break;
			case MessageType::GENERIC_3: HANDLE_MESSAGE(onReadGeneric3Request, Generic3Message) break;
			case MessageType::GENERIC_4: HANDLE_MESSAGE(onReadGeneric4Request, Generic4Message) break;
			case MessageType::GENERIC_5: HANDLE_MESSAGE(onReadGeneric5Request, Generic5Message) break;
			case MessageType::MENU:      HANDLE_MESSAGE(onReadMenuRequest,     MenuMessage) break;
			case MessageType::UNKNOWN:
			default:
				selector.writeToFD(fd, std::make_shared<Buffer>("Unknown message!\n"));
				fprintf(stdout, "Unknown message!\n");
				break;
		}
		buffer.getNext(&message, sizeof(Message));
	}
}

std::string TCPServer::createGreeting() {
	return "Welcome!\n\n" + createMenu();
}

std::string TCPServer::createMenu() {
	return "Available Commands:\n"
		   "    hello      Custom server greeting\n"
		   "    1,2,3,4,5  Each number provides a different message\n"
	 	   "    passwd     In a future version, this will allow you to change your password\n"
	 	   "    menu       Displays this menu\n"
		   "    exit       Disconnects you from the server\n";
}

void TCPServer::onReadHelloRequest(int fd, const std::shared_ptr<StoredDataType> &data, HelloMessage msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("Hello there.\n"));
}

void TCPServer::onReadGeneric1Request(int fd, const std::shared_ptr<StoredDataType> &data, Generic1Message msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("So uncivilized\n"));
}

void TCPServer::onReadGeneric2Request(int fd, const std::shared_ptr<StoredDataType> &data, Generic2Message msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("I don't like sand. It's coarse and rough and irritating... and it gets everywhere\n"));
}

void TCPServer::onReadGeneric3Request(int fd, const std::shared_ptr<StoredDataType> &data, Generic3Message msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("Now this is podracing\n"));
}

void TCPServer::onReadGeneric4Request(int fd, const std::shared_ptr<StoredDataType> &data, Generic4Message msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("I AM the Senate.\n"));
}

void TCPServer::onReadGeneric5Request(int fd, const std::shared_ptr<StoredDataType> &data, Generic5Message msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>("*kills younglings*\n"));
}

void TCPServer::onReadMenuRequest(int fd, const std::shared_ptr<StoredDataType> &data, MenuMessage msg) {
	selector.writeToFD(fd, std::make_shared<Buffer>(createMenu()));
}
