#pragma once

#include <string>
#include <Client.h>
#include <Selector.h>

class TCPClient : public Client {
	Selector<void> selector;
	int fd;
	
	public:
	TCPClient();
	~TCPClient() override = default;
	
	void connectTo(const char *ip_addr, unsigned short port) override;
	void handleConnection() override;
	
	void closeConn() override;
	
	private:
	void onRead(int fd, const std::shared_ptr<void>& data, DynamicBuffer & buffer);
	
	void handleUserInput(std::string input);
};
