module;
#include "grid.pb.h"
#include <print>
import game;
export module battleships_client;

export void client() {
	std::println("Hello world");
	Game::Grid g1{1111};
	Game::Grid g2{2222};
	Game::Game ga{std::move(g1), std::move(g2)};
	Messages::Grid pg;
	for (const auto& ship : g1.ships) {
		auto ps{pg.add_ships()};
		for (const auto& point : ship) {
			auto pp{ps->add_points()};
			pp->set_x(point.x);
			pp->set_y(point.y);
		}
	}
	std::println("{}", pg.DebugString());
}
