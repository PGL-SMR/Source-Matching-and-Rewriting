add_library(Frontend
	STATIC
	F18Frontend.hpp
    CFrontend.cpp CFrontend.hpp
	PolygeistFrontend.hpp
	FrontendInterface.hpp
	Source.hpp
	Manager.cpp Manager.hpp)

target_link_libraries(Frontend
	PUBLIC
	${LLVM_LIBS}
    ${CLANG_LIBS}
	${MLIR_LIBS}
	Logger
)

target_include_directories(Frontend
	PUBLIC
	${CMAKE_SOURCE_DIR}/smr
	${CMAKE_CURRENT_SOURCE_DIR}
)
