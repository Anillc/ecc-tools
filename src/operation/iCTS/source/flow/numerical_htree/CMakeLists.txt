set(ICTS_FLOW_NUMERICAL_HTREE_SRC
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeBuilder.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeComparison.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeLevelSearch.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeModelAdapter.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeModelQuality.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreePatternEvaluation.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeResponseSurface.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeSearch.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeStateSelection.cc
  ${ICTS_FLOW_NUMERICAL_HTREE}/NumericalHTreeTopologyAdapter.cc
)

add_library(icts_source_flow_numerical_htree ${ICTS_FLOW_NUMERICAL_HTREE_SRC})

target_link_libraries(
  icts_source_flow_numerical_htree
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_module
  icts_source_module_numerical_characterization
  icts_source_utils
)

target_include_directories(
  icts_source_flow_numerical_htree
  PUBLIC
  ${ICTS_FLOW}
)
