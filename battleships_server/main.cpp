#include <print>
#include <stdexcept>
#include <unistd.h>
import battleships_server;

int main() {
	try {
		Server server{};
		server.run();
	} catch (const std::exception& e) {
		std::println(stderr, "Server exited with the following error:");
		std::println(stderr, "{}", e.what());
		return EXIT_FAILURE;
	}
}
