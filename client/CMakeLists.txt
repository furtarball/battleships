target_link_libraries(BattleshipsClient
	PRIVATE
		game
)
target_sources(BattleshipsClient
	PRIVATE
		main.cpp
	PRIVATE FILE_SET CXX_MODULES
		FILES battleships_client.cppm
)
