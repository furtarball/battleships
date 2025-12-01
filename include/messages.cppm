module;
#include "grid.pb.h"
#include <netinet/in.h>
import game;

export module messages;

export std::string serialize(const Messages::Wire& pbmsg) {
	std::string r(sizeof(uint32_t), '\0');
	pbmsg.AppendToString(&r);
	uint32_t* len{reinterpret_cast<uint32_t*>(r.data())};
	*len = htonl(r.length() - sizeof(uint32_t));
	return r;
}

export Messages::Grid pb_obj_from_grid(const Game::Grid& g) {
	Messages::Grid pg;
	for (const auto& ship : g.ships) {
		auto ps{pg.add_ships()};
		for (const auto& point : ship) {
			auto pp{ps->add_points()};
			pp->set_x(point.x);
			pp->set_y(point.y);
		}
	}
	return pg;
}

export Game::Grid grid_from_pb_obj(const Messages::Grid& pg) {
	std::vector<std::unordered_set<Game::Point>> ships;
	for (const auto& pship : pg.ships()) {
		std::unordered_set<Game::Point> points;
		for (const auto& ppoint : pship.points()) {
			points.emplace(ppoint.x(), ppoint.y());
		}
		ships.emplace_back(std::move(points));
	}
	return Game::Grid{std::move(ships)};
}
