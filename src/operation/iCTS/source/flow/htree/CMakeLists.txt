add_library(
  icts_source_flow_htree
  ${ICTS_FLOW_HTREE}/HTreeBuilder.cc
)

target_link_libraries(
  icts_source_flow_htree
  PRIVATE
  icts_source_database
  icts_source_module
  icts_source_utils
)

target_include_directories(
  icts_source_flow_htree
  PUBLIC
  ${ICTS_FLOW}
)

