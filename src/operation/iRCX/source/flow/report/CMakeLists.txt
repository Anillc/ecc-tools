add_library(ircx_report SpefDumper.cc)

target_link_libraries(ircx_report
  PUBLIC
    ircx_database_design
    ircx_headers
  PRIVATE
    ircx_topology
    itf_data
    log
    usage
)

target_include_directories(ircx_report
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
