module;
#include <print>
import game;
export module battleships_client;

export void client() {
	std::println("Hello world");
	Grid g1{1111};
	Grid g2{2222};
	Game ga{std::move(g1), std::move(g2)};
}
