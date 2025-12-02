module;
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>
export module game;

export namespace Game {

// types of ships and number of ships of given type
const std::map<size_t, size_t, std::greater<size_t>> ships_count{
	{5, 1}, {4, 1}, {3, 2}, {2, 2}, {1, 2}};
constexpr size_t width{10}, height{10};

struct Point {
	size_t x;
	size_t y;
	Point(size_t px, size_t py) : x{px}, y{py} {
		if ((px > width) || (py > height)) {
			throw std::range_error{"Point coordinates outside grid"};
		}
	}
	bool operator==(const Point& b) const { return (x == b.x) && (y == b.y); }
};

} // namespace Game

namespace std {
template <> struct hash<Game::Point> {
	size_t operator()(const Game::Point& p) const {
		return std::hash<decltype(Game::Point::x)>{}(p.x) ^
			   std::hash<decltype(Game::Point::y)>{}(p.y);
	}
};
} // namespace std

export namespace Game {

class Grid {
	/* random number in range [0, ub) */
	std::mt19937::result_type rrand(std::mt19937& r, uint32_t ub) {
		std::mt19937::result_type res{};
		do {
			res = r();
		} while (res >= (r.max() - r.max() % ub));
		return res % ub;
	}

	bool point_occupied(Point p) {
		for (const auto& i : ships) {
			if (i.contains(p)) {
				return true;
			}
		}
		return false;
	}

	void randomize(uint32_t seed) {
		std::mt19937 r{seed};
		for (auto& [size, number] : ships_count) {
			for (size_t i = 0; i < number; i++) {
				decltype(ships)::value_type points;
				// starting point for a ship
				Point s{rrand(r, width), rrand(r, height)};
				do {
					s = Point{rrand(r, width), rrand(r, height)};
				} while (point_occupied(s));
				points.insert(s);
				// remaining points of a ship
				size_t j = 0;
				while (j < (size - 1)) {
					// pick starting point at random
					auto n{rrand(r, points.size())};
					auto start{points.begin()};
					std::advance(start, n);
					size_t x{start->x}, y{start->y};
					enum Direction { VERTICAL, HORIZONTAL };
					auto dir{rrand(r, 2)};
					auto val{rrand(r, 2)}; // up (left) or down (right)
					if (dir == VERTICAL) {
						y += (val == 0) ? (1) : (-val);
					} else if (dir == HORIZONTAL) {
						x += (val == 0) ? (1) : (-val);
					}
					if ((x >= width) || (y >= height)) {
						continue;
					}
					if (point_occupied(Point{x, y})) {
						continue;
					}
					auto status{points.emplace(x, y)};
					if (status.second == false) {
						continue;
					}
					j++;
				}
				ships.emplace_back(std::move(points));
			}
		}
	}

	public:
	std::vector<std::unordered_set<Point>> ships;
	Grid(Grid&& g) : ships{g.ships} {}
	Grid(decltype(ships)&& s) : ships{s} {}
	Grid(uint32_t seed) { randomize(seed); }
	size_t count() {
		return std::accumulate(ships.begin(), ships.end(), 0,
			[](size_t sum, const decltype(ships)::value_type& pts) {
				return sum + pts.size();
			});
	}
};

class Game {
	int id1, id2;
	Grid grid1, grid2;
	std::unordered_set<Point> shots1, shots2;
	size_t shots_accurate1{}, shots_accurate2{};

	public:
	std::pair<int, int> players() { return {id1, id2}; }
	std::pair<int, bool> shoot(
		int player, decltype(Point::x) x, decltype(Point::y) y) {
		decltype(shots1)*shots_a{}, *shots_b{};
		Grid* grid_b{};
		size_t* shots_accurate{};
		// who's shooting, whose fleet is being shot at
		if (player == id1) {
			shots_a = &shots1;
			shots_b = &shots2;
			grid_b = &grid2;
			shots_accurate = &shots_accurate1;
		} else if (player == id2) {
			shots_a = &shots2;
			shots_b = &shots1;
			grid_b = &grid1;
			shots_accurate = &shots_accurate2;
		} else { // error
			return {-1, false};
		}
		if (shots_a->size() > shots_b->size()) {
			// if player is shooting outside of their turn
			return {-1, false};
		}
		if (shots_a->contains({x, y})) {
			// if this shot was already made
			return {-1, false};
		} else { // finally
			shots_a->emplace(x, y);
			for (const auto& s : grid_b->ships) {
				if (s.contains({x, y})) {
					*shots_accurate += 1;
					shots_a->emplace(x, y);
					// check if whole ship was shot down
					for (const auto& p : s) {
						if (!shots_a->contains(p)) {
							return {s.size(), false};
						}
					}
					return {s.size(), true};
				}
			}
			return {0, false};
		}
	}
	bool player_won(int id) {
		if (id == id1) {
			return shots_accurate1 == grid2.count();
		}
		if (id == id2) {
			return shots_accurate2 == grid1.count();
		}
		return false;
	}
	Game(int i1, int i2, Grid&& p1, Grid&& p2)
		: id1{i1}, id2{i2}, grid1{std::forward<Grid>(p1)},
		  grid2{std::forward<Grid>(p2)} {
		// TODO random assignment of players
#ifndef NDEBUG
		// std::println("New game between {} and {}", id1, id2);
		// grid1.print();
		// grid2.print();
#endif
	}
};

} // namespace Game
