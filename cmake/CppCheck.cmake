if(ENABLE_CPPCHECK)
  find_program(CPPCHECK cppcheck)
  if(CPPCHECK)
    set(CMAKE_CXX_CPPCHECK
        ${CPPCHECK}
        --suppressions-list=${CMAKE_SOURCE_DIR}/.suppressions
        --enable=all
        --inconclusive
        --inline-suppr
        --std=c++${CMAKE_CXX_STANDARD}
        # --check-config
    )
    if(NOT EXISTS ${MY_DIRECTORY})
      file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/cppcheck)
    endif()
    message(STATUS "Cppcheck finished setting up")
  else()
    message(SEND_ERROR "Cppcheck requested but executable not found")
  endif()
endif()
