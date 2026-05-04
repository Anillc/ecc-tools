add_library(
  icts_source_flow_synthesis_htree_solution
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/Solution.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/SolutionReport.cc
)

target_link_libraries(
  icts_source_flow_synthesis_htree_solution
  PRIVATE
  icts_source_flow_synthesis_htree_plan
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_solution
  PUBLIC
  ${ICTS_FLOW}
)
