add_library(icts_source_flow_evaluation ${ICTS_FLOW}/evaluation/ClockTreeEvaluator.cc)

target_link_libraries(
  icts_source_flow_evaluation
  PRIVATE
  icts_source_database
  icts_source_module
  icts_source_utils
)

target_include_directories(
  icts_source_flow_evaluation
  PUBLIC
  ${ICTS_FLOW}
)
