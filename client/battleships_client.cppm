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
import messages;
import wrapped_posix;
export module battleships_client;

template<typename T> T receive(int client_socket) {
	uint32_t incoming_length{};
	auto n{read(client_socket, &incoming_length, sizeof(uint32_t))};
	if (n < sizeof(uint32_t)) {
		throw PosixException{"Could not determine message length"};
	}
	incoming_length = ntohl(incoming_length);
	std::string buf(incoming_length, '\0');
	auto received{0};
	while (received < incoming_length) {
		auto n{read(client_socket, buf.data() + received, buf.length() - received)};
		if (n < 0) {
			throw PosixException{"Could not receive message"};
		}
		received += n;
	}
	T msg;
	msg.ParseFromString(buf);
	return msg;
}

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

	auto m{receive<Messages::Status>(client_socket)};
	std::println("{} {}", static_cast<int>(m.code()), m.DebugString());

	Game::Grid g1{1111};
	Messages::Grid pg{pb_obj_from_grid(g1)};
	auto serialized{serialize(pg)};
	send(client_socket, serialized.data(), serialized.length(), 0);
	std::println("{}", pg.DebugString());

	auto s{receive<Messages::Status>(client_socket)};
	std::println("{} {}", static_cast<int>(s.code()), s.DebugString());
}
