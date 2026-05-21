set(IRCX_EXTRACT_SRC
  resistance/ResistanceCalc.cc
  capacitance/CapacitanceCalc.cc
)

add_library(ircx_extract ${IRCX_EXTRACT_SRC})

target_link_libraries(ircx_extract
  PUBLIC
    ircx_cap_table
    ircx_database_design
    ircx_headers
    OpenMP::OpenMP_CXX
  PRIVATE
    ircx_topology
    log
)

target_include_directories(ircx_extract
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
