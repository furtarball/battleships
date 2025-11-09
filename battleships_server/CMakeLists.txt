target_link_libraries(BattleshipsServer
	PRIVATE
		wrapped_posix
		types
)
target_sources(BattleshipsServer
	PRIVATE
		main.cpp
	PRIVATE FILE_SET CXX_MODULES
		FILES battleships_server.cppm
)
