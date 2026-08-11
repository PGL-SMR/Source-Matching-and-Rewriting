# MIT License 
# Copyright (c) 2018-Today Michele Adduci <adduci@tutanota.com>
#
# Clang-Format instructions

find_program(CLANG_FORMAT_BIN NAMES clang-format)

if(CLANG_FORMAT_BIN)
  message(STATUS "Found: clang-format")
  file(GLOB_RECURSE CPP_SOURCE_FILES smr/**.cpp)
  file(GLOB_RECURSE CPP_HEADER_FILES smr/**.hpp)
  file(GLOB NONRECURSE TESTS_SOURCE_FILES tests/**.cpp)
  file(GLOB NONRECURSE TESTS_HEADER_FILES tests/**.h)

  add_custom_target(
    format-sources
    COMMAND clang-format --style=file -i ${CPP_SOURCE_FILES}
    COMMAND_EXPAND_LISTS VERBATIM)

  add_custom_target(
    format-headers
    COMMAND clang-format --style=file -i ${CPP_HEADER_FILES}
    COMMAND_EXPAND_LISTS VERBATIM)

  add_custom_target(
    format
    COMMENT "Running clang-format..."
    DEPENDS format-sources format-headers)
else()
  message(WARNING "clang-format not found")
endif()
