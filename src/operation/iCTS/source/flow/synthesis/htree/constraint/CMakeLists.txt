add_library(
  icts_source_flow_synthesis_htree_constraint
  ${ICTS_FLOW_SYNTHESIS_HTREE_CONSTRAINT}/Constraint.cc
)

target_link_libraries(
  icts_source_flow_synthesis_htree_constraint
  PRIVATE
  icts_source_module
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_constraint
  PUBLIC
  ${ICTS_FLOW}
)
