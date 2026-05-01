add_library(icts_source_flow_synthesis_htree_embedding ${ICTS_FLOW_SYNTHESIS_HTREE_EMBEDDING}/Embedding.cc)

target_link_libraries(
  icts_source_flow_synthesis_htree_embedding
  PUBLIC
  icts_source_flow_synthesis_htree_segment_pruning
  PRIVATE
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_embedding
  PUBLIC
  ${ICTS_FLOW}
)
