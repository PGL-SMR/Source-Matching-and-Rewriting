add_library(Rewriter
    STATIC
    Selector.hpp Selector.cpp
    Rewriter.hpp Rewriter.cpp)

target_link_libraries(Rewriter
    PUBLIC
    ${LLVM_LIBS}
    ${MLIR_LIBS}
    Common
    Logger
    Frontend
)

target_include_directories(Rewriter
    PUBLIC
    ${CMAKE_SOURCE_DIR}/smr
    ${CMAKE_CURRENT_SOURCE_DIR}
)
