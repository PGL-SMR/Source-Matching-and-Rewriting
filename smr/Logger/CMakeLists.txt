add_library(Logger
	STATIC
	Messages.hpp
	Logger.hpp Logger.cpp)

target_link_libraries(Logger
	PUBLIC
	${LLVM_LIBS}
)

target_include_directories(Logger
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)
