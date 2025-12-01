target_link_libraries(BattleshipsServer
	PRIVATE
		messages
		game
		types
		wrapped_posix
		platform
)
target_sources(BattleshipsServer
	PRIVATE
		main.cpp
	PRIVATE
		FILE_SET CXX_MODULES
		FILES battleships_server.cppm
)
