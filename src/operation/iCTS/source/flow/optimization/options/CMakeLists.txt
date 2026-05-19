add_library(
  icts_source_flow_optimization_options
  ${ICTS_FLOW_OPTIMIZATION_OPTIONS}/OptimizationOptions.cc
)

target_include_directories(
  icts_source_flow_optimization_options
  PUBLIC
  ${ICTS_FLOW}
)
