add_library(
  icts_source_flow_evaluation_qor
  ${ICTS_FLOW_EVALUATION_QOR}/QorEvaluation.cc
  ${ICTS_FLOW_EVALUATION_QOR}/QorEvaluationMetrics.cc
  ${ICTS_FLOW_EVALUATION_QOR}/QorEvaluationRootProbe.cc
)

target_link_libraries(
  icts_source_flow_evaluation_qor
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_module
  icts_source_module_timing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_evaluation_qor
  PUBLIC
  ${ICTS_FLOW}
)
