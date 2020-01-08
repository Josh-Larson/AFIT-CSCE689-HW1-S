#pragma once

#include "Server.h"

class TCPServer : public Server {
	public:
	TCPServer();
	~TCPServer() override;
	
	void bindSvr(const char *ip_addr, unsigned short port) override;
	void listenSvr() override;
	void shutdown() override;
};
