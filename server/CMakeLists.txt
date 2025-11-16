target_link_libraries(BattleshipsServer
	PRIVATE
		game
		types
		wrapped_posix
		platform
		messages
)
target_sources(BattleshipsServer
	PRIVATE
		main.cpp
	PRIVATE
		FILE_SET CXX_MODULES
		FILES battleships_server.cppm
)
