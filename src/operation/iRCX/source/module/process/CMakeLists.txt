cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Eigen3 REQUIRED)
##-------------------------- Source Files ------------------------##
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC)

##-------------------------- Library ------------------------##
add_library(ircx_process ${SRC})

##-------------------------- Link Libraries ------------------------##
target_link_libraries(ircx_process
  PUBLIC
    itf
    itf_data
)

##-------------------------- Include Directories ------------------------##
target_include_directories(ircx_process
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)