add_library(CDG
    STATIC
    Input.hpp
    Match.hpp
    CDG.hpp CDG.cpp)

target_link_libraries(CDG
    PUBLIC
    ${LLVM_LIBS}
    ${MLIR_LIBS}
    Common
    Automaton
    Logger
)

target_include_directories(CDG
    PUBLIC
    ${CMAKE_SOURCE_DIR}/smr
    ${CMAKE_CURRENT_SOURCE_DIR}
)
