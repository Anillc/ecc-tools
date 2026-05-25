add_library(icts_source_flow_synthesis_htree_segment_pruning ${ICTS_FLOW_SYNTHESIS_HTREE_SEGMENT_PRUNING}/SegmentPruning.cc)

target_link_libraries(
  icts_source_flow_synthesis_htree_segment_pruning
  PUBLIC
  icts_source_module_characterization
  PRIVATE
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_segment_pruning
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_FLOW}
)
