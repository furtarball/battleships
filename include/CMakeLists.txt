add_library(wrapped_posix)
target_sources(wrapped_posix
	PUBLIC FILE_SET HEADERS
		FILES types.h
	PUBLIC FILE_SET CXX_MODULES
		FILES wrapped_posix.cppm
)
target_compile_features(wrapped_posix PUBLIC cxx_std_23)
