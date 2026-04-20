cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##-------------------------- Source Files ------------------------##
aux_source_directory(./ SRC)

##-------------------------- Library ------------------------##
add_library(rcx_shell_cmd ${SRC})

##-------------------------- Link Libraries ------------------------##
target_link_libraries(rcx_shell_cmd
    PUBLIC
        ircx
    PRIVATE
        str
        tcl
)

##-------------------------- Include Directories ------------------------##
target_include_directories(rcx_shell_cmd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

