module;
#include <cstdint>
#include <functional>
#include <map>
#include <print>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>
export module game;

// types of ships and number of ships of given type
const std::map<size_t, size_t> ships_count{
	{5, 1}, {4, 1}, {3, 2}, {2, 2}, {1, 2}};
constexpr size_t width{10}, height{10};

export struct Point {
	size_t x;
	size_t y;
	Point(size_t px, size_t py) : x{px}, y{py} {
		if ((px > width) || (py > height)) {
			throw std::range_error{"Point coordinates outside grid"};
		}
	}
	bool operator==(const Point& b) const { return (x == b.x) && (y == b.y); }
};

namespace std {
template <> struct hash<Point> {
	size_t operator()(const Point& p) const {
		return std::hash<decltype(Point::x)>{}(p.x) ^
			   std::hash<decltype(Point::y)>{}(p.y);
	}
};
} // namespace std

export class Grid {
	std::unordered_set<Point> shots_fired;
	std::vector<std::unordered_set<Point>> ships;
	/* random number in range [0, ub) */
	std::mt19937::result_type rrand(std::mt19937& r, uint32_t ub) {
		std::mt19937::result_type res{};
		do {
			res = r();
		} while (res >= (r.max() - r.max() % ub));
		return res % ub;
	}

	void randomize(uint32_t seed) {
		std::mt19937 r{seed};
		for (auto& [size, number] : ships_count) {
			for (size_t i = 0; i < number; i++) {
				decltype(ships)::value_type points;
				auto status{points.emplace(rrand(r, width), rrand(r, height))};
				size_t j = 0;
				while (j < (size - 1)) {
					size_t x{status.first->x}, y{status.first->y};
					enum Direction { VERTICAL, HORIZONTAL };
					auto dir{rrand(r, 2)};
					auto val{rrand(r, 2)}; // up (left) or down (right)
					if (dir == VERTICAL) {
						y += (val == 0) ? (1) : (-val);
					} else if (dir == HORIZONTAL) {
						x += (val == 0) ? (1) : (-val);
					}
					if ((x > width) || (y > height)) {
						continue;
					}
					status = points.emplace(x, y);
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
	Grid(Grid&& g) : ships{g.ships} {}
	Grid(decltype(ships)&& s) : ships{s} {}
	Grid(uint32_t seed) {
		randomize(seed);
#ifndef NDEBUG
		std::print("Grid: ");
		for (auto& i : ships) {
			std::print("[ ");
			for (auto& j : i) {
				std::print("({}, {}) ", j.x, j.y);
			}
			std::print("] ");
		}
		std::println();
#endif
	}
};

export class Game {
	Grid grid1, grid2;

	public:
	Game(Grid&& p1, Grid&& p2)
		: grid1{std::forward<Grid>(p1)}, grid2{std::forward<Grid>(p2)} {}
};
