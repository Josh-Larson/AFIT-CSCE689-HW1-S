#pragma once

class TCPServer {
	public:
	TCPServer();
	~TCPServer();
	
	void bindSvr(const char *ip_addr, short unsigned int port);
	void listenSvr();
	void shutdown();
	
};