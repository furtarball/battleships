module;
#include "grid.pb.h"
#include "platform.h"
#include "types.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <netinet/in.h>
#include <print>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#else
#error "sys/event.h not found; server requires rdqueue support (BSD, Mac)"
#endif
import game;
import messages;
import wrapped_posix;

export module battleships_server;

export class Server {
	const Socket server_socket;
	std::vector<kevent_t> rdevents, wrevents;
	std::set<Socket> clients;
	std::map<int, std::deque<std::string>> sendbufs;
	std::map<int, std::pair<uint32_t, std::string>> recvbufs;
	int rdq, wrq;
	std::deque<int> waiting;
	std::map<int, int> pairs;
	std::map<int, Game::Grid> submitted_grids;
	std::map<int, std::shared_ptr<Game::Game>> games;

	public:
	Server()
		: server_socket{socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)},
		  rdevents(1), rdq(kqueue()), wrq(kqueue()) {
		if (rdq < 0) {
			throw PosixException{"Could not create rdqueue"};
		}
#ifndef NDEBUG
		std::println("debug build");
		constexpr int enable{1};
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
		EV_SET(&rdevents.at(0),
			static_cast<uintptr_t>(static_cast<int>(server_socket)),
			EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
		if (kevent(rdq, rdevents.data(), rdevents.size(), nullptr, 0, nullptr) <
			0) {
			throw PosixException{"Could not add server socket to rdqueue"};
		}
		while (true) {
			while (rdevents.size() > clients.size() + 1) {
				rdevents.pop_back();
#ifndef NDEBUG
				std::println(
					"Events: {}, clients: {}", rdevents.size(), clients.size());
#endif
			}
			auto nrdevents{kevent(
				rdq, nullptr, 0, rdevents.data(), rdevents.size(), nullptr)};
			if (nrdevents < 0) {
				throw PosixException{"Could not poll for incoming messages"};
			}
			for (int i = 0; i < nrdevents; i++) {
				const struct kevent& e{rdevents.at(i)};
				if (e.flags & EV_ERROR) {
					throw PosixException{"Error checking for incoming messages",
						static_cast<int>(e.data)};
				}
				if (e.ident ==
					static_cast<uintptr_t>(static_cast<int>(server_socket))) {
					// new connection arrived
					new_client();
				} else if (e.filter == EVFILT_READ) {
					// new message arrived
					receive(e);
				}
			}
			constexpr struct timespec timeout0{};
			auto nwrevents{kevent(
				wrq, nullptr, 0, wrevents.data(), wrevents.size(), &timeout0)};
			if (nwrevents < 0) {
				throw PosixException{
					"Could not poll for sockets ready to send on"};
			}
			for (int i = 0; i < nwrevents; i++) {
				const struct kevent& e{wrevents.at(i)};
				if (e.flags & EV_ERROR) {
					throw PosixException{
						"Error trying to send", static_cast<int>(e.data)};
				}
				if (e.filter == EVFILT_WRITE) {
					transmit(e);
				}
			}
			for (int i = 0; i < nwrevents; i++) {
				wrevents.pop_back();
			}
		}
	}
	void new_client() {
		struct sockaddr_in6 ca{};
		socklen_t addrlen{sizeof(ca)};
		int cfd{accept4(server_socket, reinterpret_cast<struct sockaddr*>(&ca),
			&addrlen, SOCK_NONBLOCK)};
		auto [it, st] = clients.emplace(cfd);
		rdevents.emplace_back();
		auto& e = rdevents.back();
		EV_SET(&e, static_cast<uintptr_t>(cfd), EVFILT_READ, EV_ADD | EV_ENABLE,
			0, 0, nullptr);
		if (kevent(rdq, &e, 1, nullptr, 0, nullptr) < 0) {
			throw PosixException{"Could not add client socket to rdqueue"};
		}
		std::println("Client: {}", ipv6_to_string(ca.sin6_addr));
		if (waiting.empty()) {
			waiting.push_back(cfd);
		} else {
			auto opponent{waiting.front()};
			waiting.pop_front();
			pairs.emplace(opponent, *it);
			pairs.emplace(*it, opponent);
			Messages::Wire awaiting_grid;
			awaiting_grid.mutable_status()->set_code(
				Messages::Status::AWAITING_GRID);
			enqueue(*it, awaiting_grid);
			enqueue(opponent, awaiting_grid);
			std::println("{} partnered with {}", cfd, opponent);
		}
	}
	void add_write_event(int recipient) {
		wrevents.emplace_back();
		auto& e = wrevents.back();
		EV_SET(&e, static_cast<uintptr_t>(recipient), EVFILT_WRITE,
			EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
		if (kevent(wrq, &e, 1, nullptr, 0, nullptr) < 0) {
			throw PosixException{"Could not enqueue message for sending"};
		}
	}
	void enqueue(int recipient, const Messages::Wire& msg) {
		sendbufs[recipient].emplace_back(serialize(msg));
		add_write_event(recipient);
	}
	void receive(const kevent_t& e) {
		auto length{e.data};
		int id{static_cast<int>(e.ident)};
		auto& [len, msg] = recvbufs[id];
		if (length > 0) {
			if (len == 0) {
				auto n{read(id, &len, sizeof(len))};
				if (n < sizeof(len)) {
					std::println(
						stderr, "Warning: could not determine message length");
				}
				len = ntohl(len);
				length -= n;
			}
			std::string buf(
				std::min(
					static_cast<decltype(length)>(len - msg.length()), length),
				'\0');
			auto n{read(id, buf.data(), buf.length())};
			if (n < 0) {
				std::println(stderr, "Warning: could not receive message");
			}
			msg += buf;
		}
		if ((length > 0) && (msg.length() >= len)) {
			handle(id);
		}
		if (e.flags & EV_EOF) {
			// handle disconnection
			if (e.fflags) { // EOF was due to error
				std::println(stderr, "Client error: {}", strerror(e.fflags));
			}
			client_cleanup(id);
		}
	}
	void client_cleanup(int c) {
		clients.erase(c);
		sendbufs.erase(c);
		recvbufs.erase(c);
		auto waiting_it{find(waiting.begin(), waiting.end(), c)};
		if (waiting_it != waiting.end()) {
			waiting.erase(waiting_it);
		}
		auto pair{pairs.extract(c)};
		if (pair) {
			auto opponent{pair.mapped()};
			client_cleanup(opponent);
		}
		submitted_grids.erase(c);
		games.erase(c);
	}
	void transmit(const kevent_t& e) {
		auto space{e.data};
		int id{static_cast<int>(e.ident)};
		if (e.flags & EV_EOF) {
			// handle disconnection
			if (e.fflags) { // EOF was due to error
				std::println(stderr, "Client error: {}", strerror(e.fflags));
			}
			client_cleanup(id);
			return;
		}
		while (!sendbufs.at(id).empty()) {
			auto& msg{sendbufs.at(id).front()};
			if (space < msg.length()) {
				// maybe we'll be able to send next time; re-add event
				add_write_event(id);
				return;
			} else {
				auto n{send(id, msg.data(), msg.length(), 0)};
				if (n < 0) {
					throw PosixException{"Could not send"};
				} else if (n < msg.length()) {
					msg.erase(0, n);
					continue;
				} else {
					sendbufs.at(id).pop_front();
				}
			}
		}
	}
	void handle(int id) {
		const auto& str{recvbufs[id].second};
		Messages::Wire w;
		if (!w.ParseFromString(str)) {
			std::println(
				stderr, "Warning: could not parse message from {}", id);
			recvbufs.erase(id);
			return;
		}
		if (w.has_status()) {
		} else if (w.has_grid()) {
			std::println("Got grid from {}", id);
			auto opponent{pairs.at(id)};
			auto& pg{*(w.mutable_grid())};
			// if we're still waiting for opponent's grid
			if (!submitted_grids.contains(opponent)) {
				submitted_grids.emplace(id, grid_from_pb_obj(pg));
			} else {
				auto new_game{std::make_shared<Game::Game>(opponent, id,
					std::move(submitted_grids.at(opponent)),
					grid_from_pb_obj(pg))};
				games.emplace(id, new_game);
				games.emplace(opponent, new_game);
				submitted_grids.erase(opponent);
				Messages::Wire whose_move;
				whose_move.mutable_status()->set_code(
					Messages::Status::YOUR_MOVE);
				enqueue(id, whose_move);
				whose_move.mutable_status()->set_code(
					Messages::Status::OPPONENTS_MOVE);
				enqueue(opponent, whose_move);
			}
		} else if (w.has_move()) {
			std::println("Got move from {}: {}", id, w.move().DebugString());
			const auto& p{w.move()};
			auto [ship_hit, sunken] = games.at(id)->shoot(id, p.x(), p.y());
			std::println("Hit status: {}", ship_hit);
			Messages::Wire resp;
			auto opponent{pairs.at(id)};
			resp.mutable_move()->set_x(p.x());
			resp.mutable_move()->set_y(p.y());
			enqueue(opponent, resp);
			if (ship_hit < 0) {
				resp.mutable_status()->set_code(Messages::Status::WRONG_MOVE);
			} else if (ship_hit == 0) {
				resp.mutable_status()->set_code(Messages::Status::MISS);
			} else {
				resp.mutable_status()->set_code(Messages::Status::HIT);
				resp.mutable_status()->set_hit_ship_size(ship_hit);
				resp.mutable_status()->set_ship_sunken(sunken);
				if (games.at(id)->player_won(id)) {
					enqueue(id, resp);
					resp.clear_status();
					resp.mutable_status()->set_code(Messages::Status::WIN);
					enqueue(id, resp);
					resp.mutable_status()->set_code(Messages::Status::LOSS);
					enqueue(opponent, resp);
					recvbufs.erase(id);
					return;
				}
			}
			enqueue(id, resp);
			if (ship_hit >= 0) {
				enqueue(opponent, resp);
				resp.clear_status();
				resp.mutable_status()->set_code(Messages::Status::YOUR_MOVE);
				enqueue(opponent, resp);
				resp.mutable_status()->set_code(
					Messages::Status::OPPONENTS_MOVE);
				enqueue(id, resp);
			}
		}
		recvbufs.erase(id);
	}
};
