module;
#include "grid.pb.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <arpa/inet.h>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <netinet/in.h>
#include <print>
#include <random>
#include <set>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
import game;
import messages;
import wrapped_posix;
export module battleships_client;

// Macro for instantiating std::unique_ptr with default deleters
template <typename T>
class Wrapped : public std::unique_ptr<T, void (*)(T*)> {};
#define WRAPPED(type, deleter)                                                 \
	template <>                                                                \
	class Wrapped<type> : public std::unique_ptr<type, void (*)(type*)> {      \
		public:                                                                \
		Wrapped(type* p)                                                       \
			: std::unique_ptr<type, void (*)(type*)>{p, deleter} {             \
			if (p == nullptr)                                                  \
				throw std::runtime_error{SDL_GetError()};                      \
		}                                                                      \
		operator type*() { return get(); }                                     \
	};
WRAPPED(SDL_Window, SDL_DestroyWindow);
WRAPPED(SDL_Renderer, SDL_DestroyRenderer);
WRAPPED(SDL_Surface, SDL_DestroySurface);
WRAPPED(SDL_Texture, SDL_DestroyTexture);
WRAPPED(TTF_Font, TTF_CloseFont);

constexpr size_t margin{6}, size{24}, hitmargin{4}, topbar{22};

std::pair<bool, Messages::Wire> receive(int client_socket, int flags = 0) {
	uint32_t incoming_length{};
	auto n{recv(client_socket, &incoming_length, sizeof(uint32_t), flags)};
	if ((flags & MSG_DONTWAIT) && (n == -1) &&
		((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
		return {false, Messages::Wire{}};
	}
	if (n == 0) {
		return {true, Messages::Wire{}};
	}
	if (n < sizeof(uint32_t)) {
		throw PosixException{"Could not determine message length"};
	}
	incoming_length = ntohl(incoming_length);
	std::string buf(incoming_length, '\0');
	auto received{0};
	while (received < incoming_length) {
		auto n{recv(client_socket, buf.data() + received,
			buf.length() - received, flags)};
		if (n < 0) {
			throw PosixException{"Could not receive message"};
		}
		received += n;
	}
	Messages::Wire msg;
	msg.ParseFromString(buf);
	return {false, msg};
}

export class Client {
	class SDLguard {
		public:
		SDLguard() {
			SDL_Init(SDL_INIT_VIDEO);
			TTF_Init();
		}
		~SDLguard() {
			SDL_Quit();
			TTF_Quit();
		}
	} sdlguard;
	Socket client_socket;
	Wrapped<SDL_Window> window;
	Wrapped<SDL_Renderer> renderer;
	Wrapped<TTF_Font> font;
	std::string status;
	std::vector<SDL_FRect> squares, my_ships, hits, misses;
	void renderstatus() {
		Wrapped<SDL_Surface> ts{TTF_RenderText_Solid(
			font, status.c_str(), 0, {255, 255, 255, 255})};
		Wrapped<SDL_Texture> tt{SDL_CreateTextureFromSurface(renderer, ts)};
		const SDL_FRect dst{
			0, 0, static_cast<float>(tt->w), static_cast<float>(tt->h)};
		SDL_RenderTexture(renderer, tt, nullptr, &dst);
	}
	void render() {
		SDL_RenderClear(renderer);
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		SDL_RenderFillRects(renderer, squares.data(), squares.size());
		SDL_SetRenderDrawColor(renderer, 128, 128, 0, 255);
		SDL_RenderFillRects(renderer, my_ships.data(), my_ships.size());
		SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
		SDL_RenderFillRects(renderer, hits.data(), hits.size());
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderFillRects(renderer, misses.data(), misses.size());
		renderstatus();
		SDL_RenderPresent(renderer);
	}

	public:
	Client(const char* addr, int port)
		: client_socket{socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)},
		  window{SDL_CreateWindow("Battleships",
			  ((size + margin) * Game::width + margin) * 2,
			  (size + margin) * Game::height + margin + topbar, 0)},
		  renderer{SDL_CreateRenderer(window, NULL)},
		  font{TTF_OpenFont((std::string{SDL_GetBasePath()} + "gallant12x22.ttf").c_str(), 22)} {
		struct sockaddr_in6 sa{};
		sa.sin6_family = AF_INET6;
		sa.sin6_port = htons(port);
		if (inet_pton(AF_INET6, addr, &(sa.sin6_addr.s6_addr)) < 0) {
			throw PosixException{"Could not parse address"};
		}
		socklen_t addrlen{sizeof(sa)};
		connect(
			client_socket, reinterpret_cast<struct sockaddr*>(&sa), addrlen);

		// Draw both grids without ships or shots (so just backgrounds)
		for (size_t x = 0; x < Game::width; x++) {
			for (size_t y = 0; y < Game::height; y++) {
				squares.emplace_back((size + margin) * x + margin,
					(size + margin) * y + margin + topbar, size, size);
				squares.emplace_back(
					(size + margin) * x + margin +
						(Game::width * (size + margin) + margin),
					(size + margin) * y + margin + topbar, size, size);
			}
		}
	}
	int run() {
		SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_ADAPTIVE);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		std::random_device r;
		Game::Grid g1{r()};
		for (const auto& s : g1.ships) {
			for (const auto& p : s) {
				my_ships.emplace_back(
					(margin + size) * p.x + margin + hitmargin,
					(margin + size) * p.y + margin + hitmargin + topbar,
					size - (2 * hitmargin), size - (2 * hitmargin));
			}
		}
		status = "Starting game";
		render();
		// receive status message
		{
			auto [dscnct, m] = receive(client_socket);
			if ((dscnct) ||
				(m.status().code() != Messages::Status::AWAITING_GRID)) {
				return 1;
			}
		}
		// send grid
		{
			Messages::Wire w;
			*(w.mutable_grid()) = pb_obj_from_grid(g1);
			auto serialized{serialize(w)};
			send(client_socket, serialized.data(), serialized.length(), 0);
		}
		bool move_made{false}, my_move{false};
		Game::Point last_move{0, 0};
		while (true) {
			SDL_Event e{};
			SDL_PollEvent(&e);
			auto [dscnct, m] = receive(client_socket, MSG_DONTWAIT);
			if (dscnct) {
				return 0;
			}
			if (m.has_move()) {
				move_made = false;
				last_move = Game::Point{m.move().x(), m.move().y()};
			} else if (m.has_status()) {
				if (m.status().code() == Messages::Status::YOUR_MOVE) {
					status = "Your move";
					my_move = true;
				} else if (m.status().code() == Messages::Status::WRONG_MOVE) {
					status = "Wrong move, try something else";
					my_move = true;
					move_made = false;
				} else if (m.status().code() ==
						   Messages::Status::OPPONENTS_MOVE) {
					status += " / opponent's move";
					my_move = false;
				} else if (m.status().code() == Messages::Status::MISS) {
					status = "Miss";
					SDL_FRect square{
						static_cast<float>(
							(size + margin) * last_move.x + margin +
							((my_move)
									? (Game::width * (size + margin) + margin)
									: (0))),
						static_cast<float>(
							(size + margin) * last_move.y + margin + topbar),
						size, size};
					misses.push_back(square);
				} else if (m.status().code() == Messages::Status::HIT) {
					status = "Hit ship of size " +
							 std::to_string(m.status().hit_ship_size());
					if (m.status().ship_sunken()) {
						status += "; sunken!";
					}
					SDL_FRect square{
						static_cast<float>(
							(size + margin) * last_move.x + margin +
							((my_move)
									? (Game::width * (size + margin) + margin)
									: (0))),
						static_cast<float>(
							(size + margin) * last_move.y + margin + topbar),
						size, size};
					hits.push_back(square);
				} else if (m.status().code() == Messages::Status::WIN) {
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
						"Battleships", "You win!", window);
					return 1;
				} else if (m.status().code() == Messages::Status::LOSS) {
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
						"Battleships", "You lose!", window);
					return 1;
				}
			}
			render();
			if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				float x, y;
				SDL_GetMouseState(&x, &y);
				x -= Game::width * (size + margin) + margin;
				x = round(x);
				if ((x < 0) || (static_cast<size_t>(x) % (size + margin) < 6)) {
					continue;
				}
				size_t square_x{static_cast<size_t>(x) / (size + margin)};
				y -= topbar;
				y = round(y);
				if ((y < 0) || (static_cast<size_t>(y) % (size + margin) < 6)) {
					continue;
				}
				size_t square_y{static_cast<size_t>(y) / (size + margin)};
				if (my_move && !move_made) {
					Messages::Wire w;
					w.mutable_move()->set_x(square_x);
					w.mutable_move()->set_y(square_y);
					auto serialized{serialize(w)};
					last_move = Game::Point{square_x, square_y};
					send(client_socket, serialized.data(), serialized.length(),
						0);
					move_made = true;
				}
			}
			if (e.type == SDL_EVENT_QUIT) {
				return 0;
			}
		}
	}
};
