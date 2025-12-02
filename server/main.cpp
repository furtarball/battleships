#include <iostream>
#include <stdexcept>
#include <unistd.h>
import battleships_server;

int main() {
	try {
		Server server{};
		server.run();
	} catch (const std::exception& e) {
		std::cerr << "Server exited with the following error:\n";
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}
