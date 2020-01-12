#pragma once

#include "Server.h"
#include "Selector.h"

class TCPServer : public Server {
	Selector<void> selector;
	
	public:
	TCPServer();
	~TCPServer() override = default;
	
	void bindSvr(const char *ip_addr, unsigned short port) override;
	void listenSvr() override;
	void shutdown() final;
	
	private:
	void onRead(int fd, const std::shared_ptr<void>& data, DynamicBuffer buffer);
};
