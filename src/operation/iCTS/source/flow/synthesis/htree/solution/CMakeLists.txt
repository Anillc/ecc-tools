add_library(
  icts_source_flow_synthesis_htree_solution
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/AnalyticalSolution.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/Solution.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/SolutionReport.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/SolutionSelection.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_SOLUTION}/StageReport.cc
)

target_link_libraries(
  icts_source_flow_synthesis_htree_solution
  PRIVATE
  icts_source_flow_synthesis_htree_analytical_solver
  icts_source_flow_synthesis_htree_compensation
  icts_source_flow_synthesis_htree_embedding
  icts_source_flow_synthesis_htree_segment_pruning
  icts_source_flow_synthesis_htree_topology_pruning
  icts_source_database_adapter_sta
  icts_source_flow_synthesis_htree_plan
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_solution
  PUBLIC
  ${ICTS_FLOW}
)
