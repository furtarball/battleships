#include <string>
import battleships_client;

int main(int argc, char** argv) {
	Client client{argv[1], std::stoi(argv[2])};
	while (client.run() > 0)
		;
}
