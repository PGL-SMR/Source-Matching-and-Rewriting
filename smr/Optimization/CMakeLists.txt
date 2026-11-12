add_library(Optimization STATIC 
		Analysis/CanonicalLoop.cpp
		Analysis/TestCanonicalLoops.cpp
		Manager.cpp
		Passes.cpp
		Transforms/Canonicalize.cpp
		Transforms/DCE.cpp
		Transforms/Inline.cpp
		Transforms/LICM.cpp
		Transforms/Tiling.cpp
		Transforms/Unroll.cpp
		Transforms/CSE.cpp
		Transforms/ConstFold.cpp
		Transforms/ConstProp.cpp
		Transforms/Mem2Reg.cpp
		Transforms/FastMath.cpp
)

target_link_libraries(Optimization
	PUBLIC
		${LLVM_LIBS}
		${MLIR_LIBS}
		${CLANG_LIBS}
		MLIRCIRTransforms
		MLIRPass
		Logger
)

target_include_directories(Optimization
	PUBLIC
		${CMAKE_SOURCE_DIR}/smr
		${CMAKE_CURRENT_SOURCE_DIR}
)
