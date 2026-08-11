add_subdirectory(Pat)
add_subdirectory(Frontend)
add_subdirectory(Logger)
add_subdirectory(Automaton)
add_subdirectory(CDG)
add_subdirectory(DDG)
add_subdirectory(Rewriter)

add_library(Common
	STATIC
	Types.hpp
	CommandLine.cpp CommandLine.hpp
	Files.cpp Files.hpp
	Data.cpp Data.hpp
)

target_link_libraries(Common
	PUBLIC
	${Boost_LIBRARIES}
	pat
	Frontend
	Logger
	Automaton
	CDG
	DDG
	Rewriter
)

target_include_directories(Common
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)


add_executable(smr main.cpp)

target_link_libraries(smr
	PUBLIC
	${LLVM_LIBS}
	${MLIR_LIBS}
    ${CLANG_LIBS}
	Common
)

target_include_directories(smr
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)
