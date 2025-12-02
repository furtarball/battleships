#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>
import battleships_client;

int main(int argc, char** argv) {
	try {
		Client client{argv[1], std::stoi(argv[2])};
		while (client.run() > 0)
			;
	} catch (std::runtime_error& e) {
		SDL_ShowSimpleMessageBox(
			SDL_MESSAGEBOX_ERROR, "Battleships", e.what(), nullptr);
	}
}
