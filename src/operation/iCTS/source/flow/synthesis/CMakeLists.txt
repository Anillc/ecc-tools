set(ICTS_FLOW_SYNTHESIS_DISTRIBUTION ${ICTS_FLOW_SYNTHESIS}/distribution)
set(ICTS_FLOW_SYNTHESIS_HTREE ${ICTS_FLOW_SYNTHESIS}/htree)
set(ICTS_FLOW_SYNTHESIS_TOPOLOGY ${ICTS_FLOW_SYNTHESIS}/topology)
set(ICTS_FLOW_SYNTHESIS_TRACE ${ICTS_FLOW_SYNTHESIS}/trace)

add_subdirectory(${ICTS_FLOW_SYNTHESIS_TRACE})
add_subdirectory(${ICTS_FLOW_SYNTHESIS_HTREE})
add_subdirectory(${ICTS_FLOW_SYNTHESIS_TOPOLOGY})
add_subdirectory(${ICTS_FLOW_SYNTHESIS_DISTRIBUTION})

add_library(icts_source_flow_synthesis ${ICTS_FLOW_SYNTHESIS}/Synthesis.cc)

target_link_libraries(
  icts_source_flow_synthesis
  PUBLIC
  icts_source_flow_synthesis_trace
  PRIVATE
  icts_source_database
  icts_source_flow_synthesis_distribution
  icts_source_flow_synthesis_htree_characterization_library
  icts_source_flow_synthesis_topology
  icts_source_flow_synthesis_trace_layout
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis
  PUBLIC
  ${ICTS_FLOW}
)
