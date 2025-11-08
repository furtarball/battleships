target_link_libraries(BattleshipsServer
	PRIVATE
		wrapped_posix
)
target_sources(BattleshipsServer
	PRIVATE
		main.cpp
)
