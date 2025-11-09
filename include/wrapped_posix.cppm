module;
#include "types.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

export module wrapped_posix;

export class PosixException : public std::runtime_error {
	std::string msg;

	public:
	PosixException(const std::string m, int err = errno)
		: std::runtime_error{m + ": " + strerror(err)} {}
};

export class Socket {
	public:
	int fd;
	Socket(int fildes) : fd{fildes} {
		if (fd < 0) {
			throw PosixException{"Unable to create socket"};
		}
	}
	constexpr operator int() const { return fd; }
	Socket(const Socket&) = delete;
	Socket(Socket&&) = delete;
	Socket& operator=(const Socket&) = delete;
	Socket& operator=(Socket&&) = delete;
	~Socket() { close(fd); }
};

export std::string ipv6_to_string(const in6_addr& addr) {
	std::string r(INET6_ADDRSTRLEN, '\0');
	if (inet_ntop(AF_INET6, &addr, r.data(), r.length()) == nullptr) {
		throw PosixException{"Could not print client address"};
	}
	return r;
}
