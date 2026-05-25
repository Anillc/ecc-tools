add_library(
  icts_source_flow_synthesis_htree_plan
  ${ICTS_FLOW_SYNTHESIS_HTREE_PLAN}/Plan.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_PLAN}/DepthPlan.cc
  ${ICTS_FLOW_SYNTHESIS_HTREE_PLAN}/DepthPlanReport.cc
)

target_link_libraries(
  icts_source_flow_synthesis_htree_plan
  PUBLIC
  icts_source_flow_synthesis_htree_region
  icts_source_flow_synthesis_htree_topology_pruning
  icts_source_flow_synthesis_htree_compensation
  PRIVATE
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis_htree_plan
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_FLOW}
)
