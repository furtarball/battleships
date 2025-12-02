module;
#include "grid.pb.h"
#include "platform.h"
#include "types.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#define KQUEUE
#else
#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#define EPOLL
#else
#error "Server requires kqueue or epoll (BSD, Linux, Mac)"
#endif
#endif
import game;
import messages;
import wrapped_posix;

export module battleships_server;

export class Server {
	const Socket server_socket;
#ifdef KQUEUE
	std::vector<kevent_t> rdevents, wrevents;
#else
	std::vector<epoll_event> rdevents, wrevents;
#endif
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
		  rdevents(1),
#ifdef KQUEUE
		  rdq(kqueue()), wrq(kqueue())
#else
		  rdq(epoll_create1(0)), wrq(epoll_create1(0))
#endif
	{
		if ((rdq < 0) || (wrq < 0)) {
			throw PosixException{"Could not create rdqueue and/or wrqueue"};
		}
#ifndef NDEBUG
		std::cerr << "debug build\n";
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
#ifdef KQUEUE
		EV_SET(&rdevents.at(0),
			static_cast<uintptr_t>(static_cast<int>(server_socket)),
			EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
		if (kevent(rdq, rdevents.data(), rdevents.size(), nullptr, 0, nullptr) <
			0)
#else
		rdevents.at(0).events = EPOLLIN;
		rdevents.at(0).data.fd = server_socket;
		if (epoll_ctl(rdq, EPOLL_CTL_ADD, server_socket, &rdevents.at(0)) < 0)
#endif
		{
			throw PosixException{"Could not add server socket to rdqueue"};
		}
		while (true) {
			while (rdevents.size() > clients.size() + 1) {
				rdevents.pop_back();
#ifndef NDEBUG
				std::cerr << "Events: " << rdevents.size()
						  << ", clients: " << clients.size() << std::endl;
#endif
			}
#ifdef KQUEUE
			auto nrdevents{kevent(
				rdq, nullptr, 0, rdevents.data(), rdevents.size(), nullptr)};
#else
			auto nrdevents{
				epoll_wait(rdq, rdevents.data(), rdevents.size(), -1)};
#endif
			if (nrdevents < 0) {
				throw PosixException{"Could not poll for incoming messages"};
			}
			for (int i = 0; i < nrdevents; i++) {
				const decltype(rdevents)::value_type& e{rdevents.at(i)};
#ifdef KQUEUE
				int id{static_cast<int>(e.ident)};
				if (e.flags & EV_ERROR)
#else
				int id{e.data.fd};
				if (e.events & EPOLLERR)
#endif
				{
					std::cerr << "Warning: read muxing error on client " << id
							  << std::endl;
				}
#ifdef KQUEUE
				if (e.ident ==
					static_cast<uintptr_t>(static_cast<int>(server_socket)))
#else
				if (e.data.fd == static_cast<int>(server_socket))
#endif
				{
					// new connection arrived
					new_client();
				}
#ifdef KQUEUE
				else if (e.filter == EVFILT_READ)
#else
				else if (e.events & EPOLLIN)
#endif
				{
					// new message arrived
					receive(e);
				}
			}
#ifdef KQUEUE
			constexpr struct timespec timeout0{};
			auto nwrevents{kevent(
				wrq, nullptr, 0, wrevents.data(), wrevents.size(), &timeout0)};
#else
			auto nwrevents{
				(wrevents.size() > 0)
					? (epoll_wait(wrq, wrevents.data(), wrevents.size(), 0))
					: (0)};
#endif
			if (nwrevents < 0) {
				throw PosixException{
					"Could not poll for sockets ready to send on"};
			}
			for (int i = 0; i < nwrevents; i++) {
				const decltype(rdevents)::value_type& e{wrevents.at(i)};
#ifdef KQUEUE
				int id{static_cast<int>(e.ident)};
				if (e.flags & EV_ERROR)
#else
				int id{e.data.fd};
				if (e.events & EPOLLERR)
#endif
				{
					std::cerr << "Warning: write muxing error on client " << id
							  << std::endl;
					client_cleanup(id);
				}
#ifdef KQUEUE
				if (e.filter == EVFILT_WRITE)
#else
				if (e.events & EPOLLOUT)
#endif
				{
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
#ifdef KQUEUE
		EV_SET(&e, static_cast<uintptr_t>(cfd), EVFILT_READ, EV_ADD | EV_ENABLE,
			0, 0, nullptr);
		if (kevent(rdq, &e, 1, nullptr, 0, nullptr) < 0)
#else
		e.events = EPOLLIN;
		e.data.fd = cfd;
		if (epoll_ctl(rdq, EPOLL_CTL_ADD, cfd, &e) < 0)
#endif
		{
			std::cerr << "Warning: could not add client socket " << cfd
					  << " to rdqueue" << std::endl;
			client_cleanup(cfd);
			return;
		}
		std::cerr << "Client: " << ipv6_to_string(ca.sin6_addr) << std::endl;
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
			std::cerr << cfd << " partnered with " << opponent << std::endl;
		}
	}
	void add_write_event(int recipient) {
		wrevents.emplace_back();
		auto& e = wrevents.back();
#ifdef KQUEUE
		EV_SET(&e, static_cast<uintptr_t>(recipient), EVFILT_WRITE,
			EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
		if (kevent(wrq, &e, 1, nullptr, 0, nullptr) < 0)
#else
		e.events = EPOLLOUT;
		e.data.fd = recipient;
		// ignore EEXIST (repeated call to epoll_ctl for the same fd)
		if ((epoll_ctl(wrq, EPOLL_CTL_ADD, recipient, &e) < 0) &&
			(errno != EEXIST))
#endif
		{
			std::cerr << "Warning: could not enqueue message for sending to "
					  << recipient << ": " << strerror(errno) << std::endl;
			client_cleanup(recipient);
		}
	}
	void enqueue(int recipient, const Messages::Wire& msg) {
		sendbufs[recipient].emplace_back(serialize(msg));
		add_write_event(recipient);
	}
#ifdef KQUEUE
	void receive(const kevent_t& e) {
		auto length{e.data};
		int id{static_cast<int>(e.ident)};
#else
	void receive(const epoll_event& e) {
		int id{e.data.fd};
		int length{};
		auto r{ioctl(id, FIONREAD, &length)};
		if (r < 0) {
			std::cerr << "Warning: could not get incoming message length from "
					  << id << ": " << strerror(errno) << std::endl;
			client_cleanup(id);
			return;
		}
#endif
		auto& [len, msg] = recvbufs[id];
		if (length > 0) {
			if (len == 0) {
				auto n{read(id, &len, sizeof(len))};
				if (n < sizeof(len)) {
					std::cerr
						<< "Warning: could not determine message length from "
						<< id << std::endl;
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
				std::cerr << "Warning: could not receive message from " << id
						  << ": " << strerror(errno) << std::endl;
				client_cleanup(id);
				return;
			}
			msg += buf;
		}
		if ((length > 0) && (msg.length() >= len)) {
			handle(id);
		}
#ifdef KQUEUE
		if (e.flags & EV_EOF)
#else
		if ((length == 0) || (e.events & EPOLLERR))
#endif
		{
			// handle disconnection
#ifdef KQUEUE
			if (e.fflags) { // EOF was due to error
				std::cerr << "Warning: client error: " << strerror(e.fflags)
						  << std::endl;
			}
#endif
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
#ifdef KQUEUE
	void transmit(const kevent_t& e) {
		auto space{e.data};
		int id{static_cast<int>(e.ident)};
#else
	void transmit(const epoll_event& e) {
		int id{e.data.fd};
#endif
#ifdef KQUEUE
		if (e.flags & EV_EOF)
#else
		if (e.events & EPOLLERR)
#endif
		{
			// handle disconnection
#ifdef KQUEUE
			if (e.fflags) { // EOF was due to error
				std::cerr << "Warning: client error: " << strerror(e.fflags)
						  << std::endl;
			}
#endif
			client_cleanup(id);
			return;
		}
		while (!sendbufs.at(id).empty()) {
			auto& msg{sendbufs.at(id).front()};
#ifdef KQUEUE
			if (space < msg.length()) {
				// maybe we'll be able to send next time; re-add event
				add_write_event(id);
				return;
			} else
#endif
			{
				auto n{send(id, msg.data(), msg.length(), 0)};
				if (n < 0) {
#ifdef EPOLL
					if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
						add_write_event(id);
						auto& e{wrevents.back()};
						// checking number of bytes that can be sent without
						// blocking is a BSD-only feature; as a workaround:
						// re-add event, but make it edge-triggered
						e.events |= EPOLLET;
						if (epoll_ctl(wrq, EPOLL_CTL_MOD, id, &e) >= 0) {
							continue;
						}
					}
#endif
					std::cerr << "Warning: could not send to " << id << ": "
							  << strerror(errno) << std::endl;
					client_cleanup(id);
					return;
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
			std::cerr << "Warning: could not parse message from " << id
					  << std::endl;
			recvbufs.erase(id);
			return;
		}
		if (w.has_status()) {
		} else if (w.has_grid()) {
			std::cerr << "Got grid from " << id << std::endl;
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
			std::cerr << "Got move from " << id << ": "
					  << w.move().DebugString() << std::endl;
			const auto& p{w.move()};
			auto [ship_hit, sunken] = games.at(id)->shoot(id, p.x(), p.y());
			std::cerr << "Hit status: " << ship_hit << std::endl;
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
