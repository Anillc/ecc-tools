cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##-------------------------- Source Files ------------------------##
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC)

##-------------------------- Library ------------------------##
add_library(ircx_report ${SRC})

##-------------------------- Link Libraries ------------------------##
target_link_libraries(ircx_report
  PUBLIC
    ircx_topology
)

##-------------------------- Include Directories ------------------------##
target_include_directories(ircx_report
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
