target_link_libraries(BattleshipsClient
	PRIVATE
		messages
		game
		wrapped_posix
)
find_package(SDL3)
find_package(SDL3_ttf)
target_link_libraries(BattleshipsClient
	PRIVATE
		SDL3::SDL3
		SDL3_ttf::SDL3_ttf
)
target_sources(BattleshipsClient
	PRIVATE
		main.cpp
	PRIVATE
		FILE_SET CXX_MODULES
		FILES battleships_client.cppm
)
