add_library(
  icts_source_flow_synthesis_topology_sink
  ${ICTS_FLOW_SYNTHESIS_TOPOLOGY_SINK}/SinkBranch.cc
  ${ICTS_FLOW_SYNTHESIS_TOPOLOGY_SINK}/SinkLoadClustering.cc
)

target_link_libraries(
  icts_source_flow_synthesis_topology_sink
  PRIVATE
  icts_source_flow_synthesis_htree
  icts_source_module_topology
  icts_source_module_topology_clustering
  icts_source_database
  icts_source_flow_synthesis_topology_buffer
  icts_source_flow_synthesis_trace_distance
  icts_source_flow_synthesis_trace_topology_result
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_topology_sink
  PUBLIC
  ${ICTS_FLOW}
)
