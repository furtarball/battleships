include(CheckIncludeFiles)
check_include_files("sys/event.h" HAVE_SYS_EVENT_H)
include(CheckFunctionExists)
check_function_exists(kqueue HAVE_KQUEUE)
check_function_exists(kevent HAVE_KEVENT)
configure_file(platform.h.in ${CMAKE_CURRENT_BINARY_DIR}/platform.h)
add_library(platform)
target_sources(platform
	PUBLIC
	FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}
		FILES ${CMAKE_CURRENT_BINARY_DIR}/platform.h
)
set_target_properties(platform PROPERTIES LINKER_LANGUAGE CXX)
add_library(types)
target_sources(types
	PUBLIC
	FILE_SET HEADERS
		FILES types.h
)
target_link_libraries(types
	PUBLIC
		platform
)
set_target_properties(types PROPERTIES LINKER_LANGUAGE CXX)
add_library(wrapped_posix)
target_sources(wrapped_posix
	PUBLIC FILE_SET CXX_MODULES
		FILES wrapped_posix.cppm
)
target_link_libraries(wrapped_posix
	PUBLIC
		platform
		types
)
target_compile_features(wrapped_posix PUBLIC cxx_std_23)
add_library(game)
target_sources(game
	PUBLIC FILE_SET CXX_MODULES
		FILES game.cppm
)
target_compile_features(game PUBLIC cxx_std_23)
