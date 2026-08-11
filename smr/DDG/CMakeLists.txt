add_library(DDG
    STATIC
    Input.hpp
    Match.hpp
    Regions.hpp
    Globals.hpp
    Memory.hpp
    Utils.hpp
    StringSet.hpp
    Attributes.hpp
    yaml.hpp
    Pattern.hpp
    DDG.hpp DDG.cpp)

target_link_libraries(DDG
    PUBLIC
    ${LLVM_LIBS}
    ${MLIR_LIBS}
    Common
    Automaton
    Logger
    Frontend
)

target_include_directories(DDG
    PUBLIC
    ${CMAKE_SOURCE_DIR}/smr
    ${CMAKE_CURRENT_SOURCE_DIR}
)
