add_library(icts_source_flow_synthesis_topology_buffer ${ICTS_FLOW_SYNTHESIS_TOPOLOGY_BUFFER}/BufferInsertion.cc)

target_link_libraries(
  icts_source_flow_synthesis_topology_buffer
  PRIVATE
  icts_source_module
  icts_source_module_topology
  icts_source_module_topology_clustering
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_topology_buffer
  PUBLIC
  ${ICTS_FLOW}
)
