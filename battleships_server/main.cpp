#include "socket.h"
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <print>
#include <set>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

std::string ipv6_to_string(const struct in6_addr& addr) {
	std::string r(INET6_ADDRSTRLEN, '\0');
	if (inet_ntop(AF_INET6, &addr, r.data(), r.length()) == nullptr) {
		throw PosixException{"Could not print client address"};
	}
	return r;
}

int main() {
	const Socket server_socket{socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)};
#ifndef NDEBUG
	std::println("debug build");
	const int enable = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable,
			sizeof(enable)) < 0) {
		perror("Could not set address as reusable");
		return EXIT_FAILURE;
	}
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &enable,
			sizeof(enable)) < 0) {
		perror("Could not set port as reusable");
		return EXIT_FAILURE;
	}
#endif
	struct sockaddr_in6 sa{};
	sa.sin6_family = AF_INET6;
	sa.sin6_port = htons(1100);
	if (bind(server_socket, reinterpret_cast<const struct sockaddr*>(&sa),
			sizeof(sa)) < 0) {
		perror("Could not bind");
		return EXIT_FAILURE;
	}
	if (listen(server_socket, 12) < 0) {
		perror("listen() failed");
		return EXIT_FAILURE;
	}
	auto kq = kqueue();
	if (kq < 0) {
		perror("Could not create kqueue");
		return EXIT_FAILURE;
	}
	std::vector<struct kevent> events(1);
	EV_SET(&events.at(0), static_cast<uintptr_t>(server_socket.fd), EVFILT_READ,
		EV_ADD | EV_ENABLE, 0, 0, nullptr);
	{ // block to avoid mistakenly reading old value of rc
		auto rc = kevent(kq, events.data(), events.size(), nullptr, 0, nullptr);
		if (rc < 0) {
			perror("Could not add server socket to kqueue");
			return EXIT_FAILURE;
		}
	}
	std::set<Socket> clients;
	while (true) {
		auto nevents =
			kevent(kq, nullptr, 0, events.data(), events.size(), nullptr);
		if (nevents < 0) {
			perror("Could not poll for events");
			return EXIT_FAILURE;
		}
		for (int i = 0; i < nevents; i++) {
			const struct kevent& e{events.at(i)};
			if (e.flags & EV_ERROR) {
				std::println("Error checking for events: {}", strerror(e.data));
				return EXIT_FAILURE;
			}
			if (e.ident == static_cast<uintptr_t>(
							   server_socket.fd)) { // new connection arrived
				struct sockaddr_in6 ca{};
				socklen_t ca_size{sizeof(ca)};
				auto [cfd, success] = clients.emplace(accept(server_socket,
					reinterpret_cast<struct sockaddr*>(&ca), &ca_size));
				if (!success) {
					std::println("Could not add client socket");
					return EXIT_FAILURE;
				}
				// print client's address
				std::println("Client: [{}]", ipv6_to_string(ca.sin6_addr));

				events.emplace_back(); // add one place to eventlist
				struct kevent cev{};
				EV_SET(&cev, static_cast<uintptr_t>(*cfd), EVFILT_READ,
					EV_ADD | EV_ENABLE, 0, 0, nullptr);
				auto rc = kevent(
					kq, &cev, 1, nullptr, 0, nullptr); // add client to kqueue
				if (rc < 0) {
					perror("Could not add server socket to kqueue");
					return EXIT_FAILURE;
				}
				continue;
			}
			// handle event from client
			if (e.filter == EVFILT_READ) { // message
				auto length{e.data};
				if (length > 0) {
					std::string buf(length, '\0');
					auto n{read(e.ident, buf.data(), buf.length())};
					if (n != length) {
						std::println("Warning: lengths returned by kevent() "
									 "and read() are not equal");
					}
					std::println("{}", buf);
				}
				if (e.flags & EV_EOF) {
					if (e.fflags) { // EOF due to error
						std::println("Client error: {}", strerror(e.fflags));
					}
					if (clients.erase(static_cast<decltype(clients)::key_type>(
							e.ident)) != 1) {
            std::println("Could not remove disconnected client");
					}
				}
			}
			while (events.size() > clients.size() + 1) {
				events.pop_back();
				std::println(
					"Events: {}, clients: {}", events.size(), clients.size());
			}
		}
	}
}
