set(IRCX_EXTRACT_SRC
  RCX.cpp
  resistance/ResistanceCalc.cpp
  capacitance/CapacitanceCalc.cpp
)

add_library(ircx ${IRCX_EXTRACT_SRC})

target_link_libraries(ircx
  PUBLIC
    ircx_cap_table
    ircx_headers
    ircx_mapping
  PRIVATE
    idm
    ircx_engine
    ircx_environment
    ircx_process
    ircx_topology
    ircx_report
    itf_builder
    log
)

target_include_directories(ircx
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
