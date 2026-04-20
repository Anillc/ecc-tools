cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Eigen3 REQUIRED)
find_package(OpenMP REQUIRED)
##-------------------------- Source Files ------------------------##
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} CURRENT_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/resistance SRC_RESISTANCE)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/capacitance SRC_CAPACITANCE)
set(SRC ${CURRENT_SRC} ${SRC_RESISTANCE} ${SRC_CAPACITANCE})

##-------------------------- Library ------------------------##
add_library(ircx ${SRC})

##-------------------------- Link Libraries ------------------------##
target_link_libraries(ircx
  PUBLIC
    Eigen3::Eigen
    OpenMP::OpenMP_CXX
    ircx_engine
    ircx_topology
    ircx_environment
    ircx_process
    ircx_cap_table
    ircx_report
    itf
    itf_builder
    ircx_mapping
)

##-------------------------- Include Directories ------------------------##
target_include_directories(ircx
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/resistance
    ${CMAKE_CURRENT_SOURCE_DIR}/capacitance
    ${EIGEN3_INCLUDE_DIR}
)