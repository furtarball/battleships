module;
#include "types.h"
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <print>
#include <set>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
import game;
import wrapped_posix;

export module battleships_server;

export class Server {
	const Socket server_socket;
	std::vector<kevent_t> events;
	std::set<Socket> clients;
	int kq;

	public:
	Server()
		: server_socket{socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)}, events(1),
		  kq(kqueue()) {
		if (kq < 0) {
			throw PosixException{"Could not create kqueue"};
		}
#ifndef NDEBUG
		std::println("debug build");
		const int enable = 1;
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable,
				sizeof(enable)) < 0) {
			throw PosixException{"Could not set address as reusable"};
		}
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &enable,
				sizeof(enable)) < 0) {
			throw PosixException{"Could not set port as reusable"};
		}
#endif
		struct sockaddr_in6 sa{};
		sa.sin6_family = AF_INET6;
		sa.sin6_port = htons(1100);
		if (bind(server_socket, reinterpret_cast<const struct sockaddr*>(&sa),
				sizeof(sa)) < 0) {
			throw PosixException{"Could not bind"};
		}
	}
	void run() {
		if (listen(server_socket, 12) < 0) {
			throw PosixException{"listen() failed"};
		}
		EV_SET(&events.at(0), static_cast<uintptr_t>(server_socket.fd),
			EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
		if (kevent(kq, events.data(), events.size(), nullptr, 0, nullptr) < 0) {
			throw PosixException{"Could not add server socket to kqueue"};
		}
		while (true) {
			while (events.size() > clients.size() + 1) {
				events.pop_back();
#ifndef NDEBUG
				std::println(
					"Events: {}, clients: {}", events.size(), clients.size());
#endif
			}
			auto nevents =
				kevent(kq, nullptr, 0, events.data(), events.size(), nullptr);
			if (nevents < 0) {
				throw PosixException{"Could not poll for events"};
			}
			for (int i = 0; i < nevents; i++) {
				const struct kevent& e{events.at(i)};
				if (e.flags & EV_ERROR) {
					throw PosixException{
						"Error checking for events", static_cast<int>(e.data)};
				}
				if (e.ident == static_cast<uintptr_t>(server_socket.fd)) {
					// new connection arrived
					new_client();
				} else if (e.filter == EVFILT_READ) {
					// new message arrived
					receive(e);
				}
			}
		}
	}
	void new_client() {
		struct sockaddr_in6 ca{};
		socklen_t addrlen{sizeof(ca)};
		int cfd{accept4(server_socket, reinterpret_cast<struct sockaddr*>(&ca),
			&addrlen, 0)};
		clients.emplace(cfd);
		events.emplace_back();
		auto& e = events.back();
		EV_SET(&e, static_cast<uintptr_t>(cfd), EVFILT_READ, EV_ADD | EV_ENABLE,
			0, 0, nullptr);
		if (kevent(kq, &e, 1, nullptr, 0, nullptr) < 0) {
			throw PosixException{"Could not add server socket to kqueue"};
		}
		std::println("Client: {}", ipv6_to_string(ca.sin6_addr));
	}
	void receive(const kevent_t& e) {
		auto length{e.data};
		if (length > 0) {
			std::string buf(length, '\0');
			auto n{read(e.ident, buf.data(), buf.length())};
			if (n != length) {
				std::println(stderr, "Warning: lengths returned by kevent() "
									 "and read() are not equal");
			}
			std::println("{}", buf);
		}
		if (e.flags & EV_EOF) {
			// handle disconnection
			if (e.fflags) { // EOF was due to error
				std::println(
					stderr, "Warning: client error: {}", strerror(e.fflags));
			}
			if (clients.erase(
					static_cast<decltype(clients)::key_type>(e.ident)) != 1) {
				std::println(stderr, "Warning: could not remove "
									 "disconnected client");
			}
		}
	}
	void handle() {}
};
