#pragma once

#include <stdexcept>

class socket_error : public std::runtime_error {
	public:
	explicit socket_error(const char *message) : std::runtime_error(message) {}
	explicit socket_error(const std::string &message) : std::runtime_error(message) {}
};
