cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)

find_package(GTest REQUIRED)

add_executable(pyImpSizeParserTest
  CellSizeParserTest.cpp
  ../idb_to_imp_db/CellSizeParser.cpp
)

target_include_directories(pyImpSizeParserTest
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pyImpSizeParserTest
  PRIVATE
  gtest
  gtest_main
)
