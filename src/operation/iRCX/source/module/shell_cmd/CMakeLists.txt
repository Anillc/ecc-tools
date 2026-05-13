set(IRCX_SHELL_CMD_SRC
  CmdRCXInit.cc
  CmdRCXReport.cc
  CmdRCXRun.cc
  CmdReadCorner.cc
  CmdReadMapping.cc
)

add_library(rcx_shell_cmd ${IRCX_SHELL_CMD_SRC})

target_link_libraries(rcx_shell_cmd
  PUBLIC
    tcl
  PRIVATE
    ircx
    log
)

target_include_directories(rcx_shell_cmd
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
