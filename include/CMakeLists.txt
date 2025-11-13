add_library(types)
target_sources(types
	PUBLIC FILE_SET HEADERS
		FILES types.h
)
set_target_properties(types PROPERTIES LINKER_LANGUAGE CXX)
add_library(wrapped_posix)
target_sources(wrapped_posix
	PRIVATE
		types
	PUBLIC FILE_SET CXX_MODULES
		FILES wrapped_posix.cppm
)
target_compile_features(wrapped_posix PUBLIC cxx_std_23)
add_library(game)
target_sources(game
	PUBLIC FILE_SET CXX_MODULES
		FILES game.cppm
)
target_compile_features(game PUBLIC cxx_std_23)
