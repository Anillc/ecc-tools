cmake_minimum_required(VERSION 3.11)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_BUILD_TYPE "Release")

aux_source_directory(./ SRC)

add_library(vcd ${SRC})

set(RUST_PROJECT_NAME vcd_parser)
set(RUST_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vcd_parser)
set(RUST_LIB_TYPE a)

SETUP_RUST_PROJECT()

message(STATUS "vcd parser rust lib path ${RUST_LIB_PATH}")

ADD_EXTERNAL_PROJ(vcd)

target_link_libraries(vcd ${RUST_LIB_PATH} dl)
target_include_directories(vcd PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_vcd ${CMAKE_CURRENT_SOURCE_DIR}/vcd_parser/test/test.cpp)
target_link_libraries(test_vcd vcd)
target_include_directories(test_vcd PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR})
    