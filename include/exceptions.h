#pragma once

#include <stdexcept>

// Designed to be used for an error binding or opening a network connection, etc
class socket_error : public std::runtime_error {
	public:
	explicit socket_error(const std::string &what_arg) : runtime_error(what_arg) {}
	explicit socket_error(const char *what_arg) : runtime_error(what_arg) {}
};
