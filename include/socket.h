#include <cstring>
#include <errno.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class PosixException : public std::runtime_error {
	std::string msg;

	public:
	PosixException(const std::string m)
		: std::runtime_error{m + ": " + strerror(errno)} {}
};

class Socket {
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
