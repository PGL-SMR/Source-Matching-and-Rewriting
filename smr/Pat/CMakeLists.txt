add_library(pat STATIC 
		Parser.cpp
		ConditionEvaluator.cpp
)

target_link_libraries(pat
	PUBLIC
		${LLVM_LIBS}
		${MLIR_LIBS}
		Rewriter
)

target_include_directories(pat
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
)
