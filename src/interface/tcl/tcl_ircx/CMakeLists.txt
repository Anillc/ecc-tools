add_library(tcl_ircx INTERFACE)

target_link_libraries(tcl_ircx
    INTERFACE
        tool_manager
        tcl
        rcx_shell_cmd
        str
)

target_include_directories(tcl_ircx
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
