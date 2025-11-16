module;
#include "grid.pb.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <netinet/in.h>
#include <print>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
import game;
import wrapped_posix;
export module battleships_client;

export void client(const char* addr, int port) {
	Socket client_socket{socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)};
	struct sockaddr_in6 sa{};
	sa.sin6_family = AF_INET6;
	sa.sin6_port = htons(port);
	if (inet_pton(AF_INET6, addr, &(sa.sin6_addr.s6_addr)) < 0) {
		throw PosixException{"Could not parse address"};
	}
	socklen_t addrlen{sizeof(sa)};
	connect(client_socket, reinterpret_cast<struct sockaddr*>(&sa), addrlen);
	Game::Grid g1{1111};
	Messages::Grid pg;
	for (const auto& ship : g1.ships) {
		auto ps{pg.add_ships()};
		for (const auto& point : ship) {
			auto pp{ps->add_points()};
			pp->set_x(point.x);
			pp->set_y(point.y);
		}
	}
	std::string serialized;
	pg.SerializeToString(&serialized);
	send(client_socket, serialized.data(), serialized.length(), 0);
	std::println("{}", pg.DebugString());
}
