add_library(
  icts_source_flow_optimization_mutation
  ${ICTS_FLOW_OPTIMIZATION_MUTATION}/OptimizationMutation.cc
)

target_link_libraries(
  icts_source_flow_optimization_mutation
  PUBLIC
  icts_source_flow_optimization_model
  PRIVATE
  icts_source_database_adapter_sta
  icts_source_database_design
  icts_source_utils
)

target_include_directories(
  icts_source_flow_optimization_mutation
  PUBLIC
  ${ICTS_FLOW}
)
