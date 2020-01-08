#pragma once

class TCPClient {
	public:
	TCPClient();
	~TCPClient();
	
	void connectTo(const char *ip_addr, unsigned short port);
	void handleConnection();
	void closeConn();
};