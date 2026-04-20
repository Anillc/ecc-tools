cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##-------------------------- External CMake ------------------------##
include(${HOME_CMAKE}/operation/idb.cmake)

##-------------------------- Source Files ------------------------##
add_library(ircx_engine 
    ParasiticXEngine.cpp
    ParasiticXIDBAdapter.cpp
)

##-------------------------- Link Libraries ------------------------##
target_link_libraries(ircx_engine
    PUBLIC
        log
    PRIVATE
        idb
        def_service
        lef_service
)

##-------------------------- Include Directories ------------------------##
target_include_directories(ircx_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

