add_library(icts_source_flow_synthesis_trace_topology_result ${ICTS_FLOW_SYNTHESIS_TRACE_TOPOLOGY_RESULT}/TopologyResult.cc)

target_link_libraries(
  icts_source_flow_synthesis_trace_topology_result
  PRIVATE
  icts_source_database
  icts_source_module_topology
  icts_source_module_topology_clustering
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_trace_topology_result
  PUBLIC
  ${ICTS_FLOW}
)
