add_library(Automaton STATIC
    Hash.hpp Hash.cpp
    State.hpp State.cpp
    Automaton.hpp Automaton.cpp)

target_link_libraries(Automaton
    PUBLIC
    ${LLVM_LIBS}
    ${MLIR_LIBS}
    Logger
)

target_include_directories(Automaton
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/smr
)
