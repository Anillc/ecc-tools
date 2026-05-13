set(IRCX_TOPOLOGY_SRC
  TopoPool.cpp
  TopologyBuilder.cpp
)

add_library(ircx_topology ${IRCX_TOPOLOGY_SRC})

target_link_libraries(ircx_topology
  PUBLIC
    ircx_headers
  PRIVATE
    log
    OpenMP::OpenMP_CXX
    usage
)

target_include_directories(ircx_topology
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
