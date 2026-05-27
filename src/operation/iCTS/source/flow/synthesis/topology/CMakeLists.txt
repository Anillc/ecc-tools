set(ICTS_FLOW_SYNTHESIS_TOPOLOGY_BUFFER ${ICTS_FLOW_SYNTHESIS_TOPOLOGY}/buffer)
set(ICTS_FLOW_SYNTHESIS_TOPOLOGY_SINK ${ICTS_FLOW_SYNTHESIS_TOPOLOGY}/sink)
set(ICTS_FLOW_SYNTHESIS_TOPOLOGY_TRUNK ${ICTS_FLOW_SYNTHESIS_TOPOLOGY}/trunk)

add_subdirectory(${ICTS_FLOW_SYNTHESIS_TOPOLOGY_BUFFER})
add_subdirectory(${ICTS_FLOW_SYNTHESIS_TOPOLOGY_SINK})
add_subdirectory(${ICTS_FLOW_SYNTHESIS_TOPOLOGY_TRUNK})

add_library(icts_source_flow_synthesis_topology ${ICTS_FLOW_SYNTHESIS_TOPOLOGY}/Topology.cc)

target_link_libraries(
  icts_source_flow_synthesis_topology
  PUBLIC
  icts_source_flow_synthesis_htree
  icts_source_module_topology_clustering
  PRIVATE
  icts_source_database
  icts_source_flow_synthesis_topology_sink
  icts_source_flow_synthesis_topology_trunk
  icts_source_flow_synthesis_trace
  icts_source_flow_synthesis_trace_layout
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_topology
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_FLOW}
)
